// DXC loaded from an explicit path. dxcapi.h includes WinAdapter.h on macOS
// and Linux; d3d12shader.h needs WinAdapter.h first and `interface` defined
// as `struct` (third_party/checks/include_check.cpp shows the order).
#include "backends.hpp"
#include "shared_library.hpp"

#include <dxc/dxcapi.h>

#include <dxc/WinAdapter.h>
#define interface struct
#include <directx/d3d12shader.h>
#undef interface

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Responsibility: the DXC session of backends.hpp: load DXC, compile with
// the fixed arguments and an include handler that serves the generated
// include from memory and the rest from the shader root, read DXC's DXIL
// reflection, and strip the reflection part from the container.
//
// Contract: as backends.hpp states. Every DXC object is held by ComPtr and
// released before the library handle closes (State declares the library
// first). The include handler lives on the stack of compile() for the whole
// Compile call; DXC's AddRef and Release on it only count.
//
// Algorithm, compile:
//   1. IDxcCompiler3::Compile with a DxcBuffer source, the argument list and
//      the include handler; DXC preprocesses and the handler records what it
//      opens.
//   2. Status and the error buffer on both paths, then DXC_OUT_OBJECT and
//      DXC_OUT_REFLECTION when it succeeded, and DXC_OUT_PDB when symbols
//      were asked for. No file is written by the compiler; the tool writes
//      only the artifact, the depfile and the symbol file.
//   3. IDxcUtils and the DxcBuffer over the reflection blob, then
//      IDxcUtils::CreateReflection.
//   4. RemoveReflectionData: IDxcContainerBuilder Load, RemovePart of the
//      reflection part, SerializeContainer, GetResult. The builder
//      re-hashes the container with the hash function it found in the loaded
//      one, so the stripped container stays signed.
// A crash inside DXC ends the tool, and the build reports the command.
//
// Retrieved rules: R.1 and E.6 (ComPtr and SharedLibrary own every handle),
// R.31 (ComPtr is move-only like unique_ptr), I.11, E.27, SL.con.3 (blob
// sizes are checked before copying). Performance: COPY.4, COPY.2.
namespace bw::shader_compiler::detail {
namespace {

// Owning handle to one reference of a DXC object.
template <class T> class ComPtr {
public:
  ComPtr() noexcept = default;
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  ComPtr(ComPtr&& other) noexcept : pointer_(std::exchange(other.pointer_, nullptr)) {}
  ComPtr& operator=(ComPtr&& other) noexcept {
    if (this != &other) {
      reset();
      pointer_ = std::exchange(other.pointer_, nullptr);
    }
    return *this;
  }
  ~ComPtr() { reset(); }

  void reset() noexcept {
    if (pointer_ != nullptr) {
      pointer_->Release();
      pointer_ = nullptr;
    }
  }
  [[nodiscard]] T* get() const noexcept { return pointer_; }
  T* operator->() const noexcept { return pointer_; }
  // The slot for an out-parameter; any held reference is released first.
  [[nodiscard]] void** put_void() noexcept {
    reset();
    return reinterpret_cast<void**>(&pointer_);
  }
  // Gives up the reference to the caller.
  [[nodiscard]] T* detach() noexcept { return std::exchange(pointer_, nullptr); }

private:
  T* pointer_{};
};

std::vector<std::byte> copy_blob(IDxcBlob* blob) {
  std::vector<std::byte> bytes;
  if (blob == nullptr || blob->GetBufferPointer() == nullptr) {
    return bytes;
  }
  const auto* first = static_cast<const std::byte*>(blob->GetBufferPointer());
  bytes.assign(first, first + blob->GetBufferSize());
  return bytes;
}

std::string hresult_text(std::string_view what, HRESULT result) {
  std::ostringstream text;
  text << what << " failed with HRESULT 0x" << std::hex << static_cast<std::uint32_t>(result);
  return text.str();
}

// A file that does not open is absent from that root; one that opens and
// then fails to read is a failure of its own, never taken as absence.
enum class FileRead : std::uint8_t { Read, Absent, Failed };

FileRead read_file(const std::string& path, std::string& out) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return FileRead::Absent;
  }
  out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  return stream.bad() ? FileRead::Failed : FileRead::Read;
}

// The compiler arguments carry no host path and are ASCII by construction
// (compiler_options.hpp); a non-ASCII byte is refused rather than widened
// under a locale.
std::optional<std::wstring> widen_ascii(std::string_view text) {
  std::wstring wide;
  wide.reserve(text.size());
  for (const char c : text) {
    if (static_cast<unsigned char>(c) > 0x7FU) {
      return std::nullopt;
    }
    wide += static_cast<wchar_t>(c);
  }
  return wide;
}

std::optional<std::string> narrow_ascii(const wchar_t* text) {
  std::string narrow;
  for (const wchar_t* c = text; *c != L'\0'; ++c) {
    if (*c < 0 || *c > 0x7F) {
      return std::nullopt;
    }
    narrow += static_cast<char>(*c);
  }
  return narrow;
}

// Serves "bw/generated/<shader>.hlsli" from memory and every other include
// from the first source root that holds it, recording each file it serves
// once, in the order DXC first asks for it, with the root it came from.
// Refuses absolute paths, paths that climb out of a root and non-ASCII
// names, so nothing outside every listed root is read.
class IncludeHandler final : public IDxcIncludeHandler {
public:
  IncludeHandler(IDxcUtils& utils, const DxcCompileInput& input, DxcCompileOutput& out,
                 std::string& diagnostics) noexcept
      : utils_(utils), input_(input), served_(out.includes), paths_(out.include_paths),
        diagnostics_(diagnostics) {}

  HRESULT STDMETHODCALLTYPE LoadSource(LPCWSTR filename, IDxcBlob** include_source) override {
    if (include_source == nullptr || filename == nullptr) {
      return E_POINTER;
    }
    *include_source = nullptr;
    const std::optional<std::string> requested = narrow_ascii(filename);
    const std::optional<std::string> relative =
        requested.has_value() ? normalize_include_path(*requested) : std::nullopt;
    if (!relative.has_value()) {
      diagnostics_ += "include refused (absolute, outside the source roots, or not ASCII): ";
      diagnostics_ += requested.value_or("<non-ASCII name>");
      diagnostics_ += '\n';
      return E_FAIL;
    }
    if (*relative == input_.generated_path) {
      return make_blob(input_.generated_text, include_source);
    }
    const auto known = std::find_if(served_.begin(), served_.end(), [&](const IncludedFile& file) {
      return file.path == *relative;
    });
    if (known != served_.end()) {
      return make_blob(known->text, include_source);
    }
    IncludedFile file{*relative, {}};
    for (const std::string_view root : input_.shader_roots) {
      std::string path = join_path(root, *relative);
      const FileRead read = read_file(path, file.text);
      if (read == FileRead::Failed) {
        diagnostics_ += "include opened and could not be read: ";
        diagnostics_ += path;
        diagnostics_ += '\n';
        return E_FAIL;
      }
      if (read == FileRead::Read) {
        served_.push_back(std::move(file));
        paths_.push_back(std::move(path));
        return make_blob(served_.back().text, include_source);
      }
    }
    // DXC reports the missing include in its own diagnostics.
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
    if (object == nullptr) {
      return E_POINTER;
    }
    if (IsEqualIID(iid, __uuidof(IDxcIncludeHandler)) || IsEqualIID(iid, __uuidof(IUnknown))) {
      AddRef();
      *object = static_cast<IDxcIncludeHandler*>(this);
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  // The handler outlives the Compile call that uses it; the count is kept
  // for DXC's bookkeeping and never deletes.
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override { return --references_; }

private:
  HRESULT make_blob(std::string_view text, IDxcBlob** include_source) {
    ComPtr<IDxcBlobEncoding> blob;
    // CreateBlob copies the bytes, so the blob does not depend on `text`.
    const HRESULT result =
        utils_.CreateBlob(text.data(), static_cast<UINT32>(text.size()), DXC_CP_UTF8,
                          reinterpret_cast<IDxcBlobEncoding**>(blob.put_void()));
    if (FAILED(result)) {
      return result;
    }
    *include_source = blob.detach();
    return S_OK;
  }

  IDxcUtils& utils_;
  const DxcCompileInput& input_;
  std::vector<IncludedFile>& served_;
  std::vector<std::string>& paths_;
  std::string& diagnostics_;
  ULONG references_{1};
};

std::string utf8_output(IDxcResult& result) {
  if (!result.HasOutput(DXC_OUT_ERRORS)) {
    return {};
  }
  ComPtr<IDxcBlobUtf8> errors;
  if (FAILED(
          result.GetOutput(DXC_OUT_ERRORS, __uuidof(IDxcBlobUtf8), errors.put_void(), nullptr)) ||
      errors.get() == nullptr || errors->GetStringPointer() == nullptr) {
    return {};
  }
  return std::string{errors->GetStringPointer(), errors->GetStringLength()};
}

std::optional<std::vector<std::byte>> output_bytes(IDxcResult& result, DXC_OUT_KIND kind) {
  if (!result.HasOutput(kind)) {
    return std::nullopt;
  }
  ComPtr<IDxcBlob> blob;
  if (FAILED(result.GetOutput(kind, __uuidof(IDxcBlob), blob.put_void(), nullptr))) {
    return std::nullopt;
  }
  return copy_blob(blob.get());
}

// The register class of a bound resource, or 0 for a kind no descriptor
// array holds.
char register_class(D3D_SHADER_INPUT_TYPE type) noexcept {
  switch (type) {
  case D3D_SIT_SAMPLER:
    return 's';
  case D3D_SIT_TBUFFER:
  case D3D_SIT_TEXTURE:
  case D3D_SIT_STRUCTURED:
  case D3D_SIT_BYTEADDRESS:
    return 't';
  case D3D_SIT_UAV_RWTYPED:
  case D3D_SIT_UAV_RWSTRUCTURED:
  case D3D_SIT_UAV_RWBYTEADDRESS:
  case D3D_SIT_UAV_APPEND_STRUCTURED:
  case D3D_SIT_UAV_CONSUME_STRUCTURED:
  case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
    return 'u';
  default:
    return '\0';
  }
}

// Top-level members of one reflected constant buffer. A ConstantBuffer<T>
// reflects as one variable of struct type T named like the buffer; its
// members are T's. A plain cbuffer reflects its variables directly. False
// when any descriptor cannot be read: a partial member list is never
// returned as the whole.
bool constant_buffer_members(ID3D12ShaderReflectionConstantBuffer& buffer,
                             std::vector<ReflectedMember>& members) {
  members.clear();
  D3D12_SHADER_BUFFER_DESC buffer_desc{};
  if (FAILED(buffer.GetDesc(&buffer_desc))) {
    return false;
  }
  for (UINT v = 0; v < buffer_desc.Variables; ++v) {
    ID3D12ShaderReflectionVariable* variable = buffer.GetVariableByIndex(v);
    D3D12_SHADER_VARIABLE_DESC variable_desc{};
    if (variable == nullptr || FAILED(variable->GetDesc(&variable_desc))) {
      return false;
    }
    ID3D12ShaderReflectionType* type = variable->GetType();
    D3D12_SHADER_TYPE_DESC type_desc{};
    if (type == nullptr || FAILED(type->GetDesc(&type_desc))) {
      return false;
    }
    const bool wrapped = buffer_desc.Variables == 1U && type_desc.Class == D3D_SVC_STRUCT &&
                         variable_desc.Name != nullptr && buffer_desc.Name != nullptr &&
                         std::strcmp(variable_desc.Name, buffer_desc.Name) == 0;
    if (!wrapped) {
      members.push_back(ReflectedMember{variable_desc.Name != nullptr ? variable_desc.Name : "",
                                        variable_desc.StartOffset});
      continue;
    }
    for (UINT m = 0; m < type_desc.Members; ++m) {
      ID3D12ShaderReflectionType* member_type = type->GetMemberTypeByIndex(m);
      const char* member_name = type->GetMemberTypeName(m);
      D3D12_SHADER_TYPE_DESC member_desc{};
      if (member_type == nullptr || member_name == nullptr ||
          FAILED(member_type->GetDesc(&member_desc))) {
        return false;
      }
      members.push_back(
          ReflectedMember{member_name, variable_desc.StartOffset + member_desc.Offset});
    }
  }
  return true;
}

std::string uppercase(std::string_view text) {
  std::string upper{text};
  for (char& c : upper) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  return upper;
}

} // namespace

struct DxcSession::State {
  // Declared first so it is destroyed last: every object below came from it.
  SharedLibrary library{};
  ComPtr<IDxcCompiler3> compiler{};
  ComPtr<IDxcUtils> utils{};
  ComPtr<IDxcContainerBuilder> container_builder{};
};

DxcSession::DxcSession(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {}
DxcSession::DxcSession(DxcSession&& other) noexcept = default;
DxcSession& DxcSession::operator=(DxcSession&& other) noexcept = default;
DxcSession::~DxcSession() = default;

BackendStatus DxcSession::open(std::string_view library_path,
                               std::unique_ptr<DxcSession>& session) {
  session.reset();
  auto state = std::make_unique<State>();
  std::string error;
  state->library = SharedLibrary::open(library_path, error);
  if (!state->library.loaded()) {
    return BackendStatus{"cannot load DXC from '" + std::string{library_path} + "': " + error,
                         shaders::ExitCode::ToolchainUnavailable};
  }
  void* address = state->library.symbol("DxcCreateInstance", error);
  if (address == nullptr) {
    return BackendStatus{"DXC library has no DxcCreateInstance: " + error,
                         shaders::ExitCode::ToolchainUnavailable};
  }
  const auto create = reinterpret_cast<DxcCreateInstanceProc>(address);
  HRESULT result = create(CLSID_DxcCompiler, __uuidof(IDxcCompiler3), state->compiler.put_void());
  if (FAILED(result)) {
    return BackendStatus{hresult_text("DxcCreateInstance(CLSID_DxcCompiler)", result),
                         shaders::ExitCode::ToolchainUnavailable};
  }
  result = create(CLSID_DxcUtils, __uuidof(IDxcUtils), state->utils.put_void());
  if (FAILED(result)) {
    return BackendStatus{hresult_text("DxcCreateInstance(CLSID_DxcUtils)", result),
                         shaders::ExitCode::ToolchainUnavailable};
  }
  result = create(CLSID_DxcContainerBuilder, __uuidof(IDxcContainerBuilder),
                  state->container_builder.put_void());
  if (FAILED(result)) {
    return BackendStatus{hresult_text("DxcCreateInstance(CLSID_DxcContainerBuilder)", result),
                         shaders::ExitCode::ToolchainUnavailable};
  }
  session.reset(new DxcSession(std::move(state)));
  return BackendStatus{};
}

BackendStatus DxcSession::identity(std::string& out) const {
  ComPtr<IDxcVersionInfo2> version;
  HRESULT result = state_->compiler->QueryInterface(__uuidof(IDxcVersionInfo2), version.put_void());
  if (FAILED(result)) {
    return BackendStatus{hresult_text("IDxcCompiler3::QueryInterface(IDxcVersionInfo2)", result),
                         shaders::ExitCode::ToolchainUnavailable};
  }
  UINT32 major = 0;
  UINT32 minor = 0;
  result = version->GetVersion(&major, &minor);
  if (FAILED(result)) {
    return BackendStatus{hresult_text("IDxcVersionInfo::GetVersion", result),
                         shaders::ExitCode::ToolchainUnavailable};
  }
  UINT32 commit_count = 0;
  char* commit_hash = nullptr;
  result = version->GetCommitInfo(&commit_count, &commit_hash);
  if (FAILED(result) || commit_hash == nullptr) {
    return BackendStatus{hresult_text("IDxcVersionInfo2::GetCommitInfo", result),
                         shaders::ExitCode::ToolchainUnavailable};
  }
  std::ostringstream text;
  text << "dxcompiler " << major << '.' << minor << '(' << commit_count << '-' << commit_hash
       << ')';
  // DXC allocates the string with CoTaskMemAlloc, which WinAdapter.h maps to
  // malloc off Windows; CoTaskMemFree is its pair.
  CoTaskMemFree(commit_hash);
  out = text.str();
  return BackendStatus{};
}

BackendStatus DxcSession::compile(const DxcCompileInput& input, DxcCompileOutput& out) const {
  out = DxcCompileOutput{};
  std::vector<std::wstring> wide_arguments;
  wide_arguments.reserve(input.arguments.size());
  for (const std::string& argument : input.arguments) {
    std::optional<std::wstring> wide = widen_ascii(argument);
    if (!wide.has_value()) {
      return BackendStatus{"compiler argument is not ASCII: " + argument,
                           shaders::ExitCode::ContractViolation};
    }
    wide_arguments.push_back(std::move(*wide));
  }
  std::vector<LPCWSTR> argument_pointers;
  argument_pointers.reserve(wide_arguments.size());
  for (const std::wstring& argument : wide_arguments) {
    argument_pointers.push_back(argument.c_str());
  }

  std::string handler_diagnostics;
  IncludeHandler handler(*state_->utils.get(), input, out, handler_diagnostics);
  const DxcBuffer source{input.source.data(), input.source.size(), DXC_CP_UTF8};
  ComPtr<IDxcResult> result;
  // Step 1.
  const HRESULT called = state_->compiler->Compile(
      &source, argument_pointers.data(), static_cast<UINT32>(argument_pointers.size()), &handler,
      __uuidof(IDxcResult), result.put_void());
  if (FAILED(called) || result.get() == nullptr) {
    return BackendStatus{hresult_text("IDxcCompiler3::Compile", called) + "\n" +
                             handler_diagnostics,
                         shaders::ExitCode::CompileError};
  }
  // Step 2. The error buffer is read on both paths, whatever the status.
  // With -WX a successful compile has nothing to say, so a message on a
  // successful status is treated as a failure rather than dropped.
  HRESULT status = S_OK;
  const bool succeeded = SUCCEEDED(result->GetStatus(&status)) && SUCCEEDED(status);
  const std::string messages = handler_diagnostics + utf8_output(*result.get());
  if (!succeeded) {
    return BackendStatus{messages, shaders::ExitCode::CompileError};
  }
  if (!messages.empty()) {
    return BackendStatus{"DXC reported success with messages:\n" + messages,
                         shaders::ExitCode::CompileError};
  }
  std::optional<std::vector<std::byte>> object = output_bytes(*result.get(), DXC_OUT_OBJECT);
  if (!object.has_value() || object->empty()) {
    return BackendStatus{"DXC reported success with no object output",
                         shaders::ExitCode::CompileError};
  }
  out.object = std::move(*object);
  out.reflection =
      output_bytes(*result.get(), DXC_OUT_REFLECTION).value_or(std::vector<std::byte>{});
  out.pdb = output_bytes(*result.get(), DXC_OUT_PDB).value_or(std::vector<std::byte>{});
  return BackendStatus{};
}

BackendStatus DxcSession::reflect(std::span<const std::byte> reflection, shaders::ShaderStage stage,
                                  BackendReflection& out) const {
  out = BackendReflection{};
  if (reflection.empty()) {
    return BackendStatus{"DXC produced no reflection for a DXIL target",
                         shaders::ExitCode::CompileError};
  }
  // Step 3.
  const DxcBuffer buffer{reflection.data(), reflection.size(), 0};
  ComPtr<ID3D12ShaderReflection> shader;
  HRESULT result =
      state_->utils->CreateReflection(&buffer, __uuidof(ID3D12ShaderReflection), shader.put_void());
  if (FAILED(result)) {
    return BackendStatus{hresult_text("IDxcUtils::CreateReflection", result),
                         shaders::ExitCode::CompileError};
  }
  D3D12_SHADER_DESC desc{};
  result = shader->GetDesc(&desc);
  if (FAILED(result)) {
    return BackendStatus{hresult_text("ID3D12ShaderReflection::GetDesc", result),
                         shaders::ExitCode::CompileError};
  }
  for (UINT i = 0; i < desc.BoundResources; ++i) {
    D3D12_SHADER_INPUT_BIND_DESC bind{};
    if (FAILED(shader->GetResourceBindingDesc(i, &bind))) {
      return BackendStatus{"ID3D12ShaderReflection::GetResourceBindingDesc failed",
                           shaders::ExitCode::CompileError};
    }
    const std::string name = bind.Name != nullptr ? bind.Name : "";
    if (bind.Type == D3D_SIT_CBUFFER) {
      const std::optional<BlockBinding> binding =
          classify_dxil_constant_buffer(bind.BindPoint, bind.Space);
      if (!binding.has_value()) {
        return BackendStatus{
            "constant buffer '" + name + "' at b" + std::to_string(bind.BindPoint) + ", space" +
                std::to_string(bind.Space) + " is not an element of the root layout",
            shaders::ExitCode::ContractViolation};
      }
      if (binding->kind == BlockBindingKind::RootConstants) {
        out.uses_root_constants = true;
        continue;
      }
      ID3D12ShaderReflectionConstantBuffer* buffer_reflection =
          shader->GetConstantBufferByName(bind.Name);
      BoundBlock block{*binding, {}};
      if (buffer_reflection == nullptr ||
          !constant_buffer_members(*buffer_reflection, block.members)) {
        std::string message = "DXC reflection of constant buffer '";
        message += name;
        message += "' could not be read whole";
        return BackendStatus{message, shaders::ExitCode::CompileError};
      }
      out.blocks.push_back(std::move(block));
      continue;
    }
    const char klass = register_class(bind.Type);
    const std::optional<shaders::DescriptorArray> array =
        klass == '\0' ? std::nullopt : descriptor_array_for_register(klass, bind.Space);
    if (!array.has_value() || bind.BindPoint != 0U) {
      return BackendStatus{"resource '" + name + "' at register " + std::to_string(bind.BindPoint) +
                               ", space" + std::to_string(bind.Space) +
                               " is not a descriptor array of the root layout",
                           shaders::ExitCode::ContractViolation};
    }
    out.descriptor_arrays |= 1U << static_cast<std::uint32_t>(*array);
  }

  if (stage == shaders::ShaderStage::Vertex) {
    std::vector<D3D12_SIGNATURE_PARAMETER_DESC> inputs;
    for (UINT i = 0; i < desc.InputParameters; ++i) {
      D3D12_SIGNATURE_PARAMETER_DESC parameter{};
      if (FAILED(shader->GetInputParameterDesc(i, &parameter))) {
        return BackendStatus{"ID3D12ShaderReflection::GetInputParameterDesc failed",
                             shaders::ExitCode::CompileError};
      }
      if (keeps_vertex_input(parameter.SemanticName != nullptr ? parameter.SemanticName : "",
                             parameter.SystemValueType != D3D_NAME_UNDEFINED)) {
        inputs.push_back(parameter);
      }
    }
    std::stable_sort(inputs.begin(), inputs.end(),
                     [](const auto& a, const auto& b) { return a.Register < b.Register; });
    std::uint32_t location = 0;
    for (const D3D12_SIGNATURE_PARAMETER_DESC& parameter : inputs) {
      out.vertex_inputs.push_back(BackendVertexInput{
          Semantic{uppercase(parameter.SemanticName != nullptr ? parameter.SemanticName : ""),
                   parameter.SemanticIndex},
          location});
      ++location;
    }
  }
  if (stage == shaders::ShaderStage::Compute) {
    UINT x = 0;
    UINT y = 0;
    UINT z = 0;
    shader->GetThreadGroupSize(&x, &y, &z);
    out.thread_group_size = {x, y, z};
  }
  return BackendStatus{};
}

BackendStatus DxcSession::remove_reflection_part(std::vector<std::byte>& container) const {
  // Step 4.
  ComPtr<IDxcBlobEncoding> input;
  HRESULT result =
      state_->utils->CreateBlob(container.data(), static_cast<UINT32>(container.size()), DXC_CP_ACP,
                                reinterpret_cast<IDxcBlobEncoding**>(input.put_void()));
  if (FAILED(result)) {
    return BackendStatus{hresult_text("IDxcUtils::CreateBlob", result),
                         shaders::ExitCode::CompileError};
  }
  result = state_->container_builder->Load(input.get());
  if (FAILED(result)) {
    return BackendStatus{hresult_text("IDxcContainerBuilder::Load", result),
                         shaders::ExitCode::CompileError};
  }
  result = state_->container_builder->RemovePart(DXC_PART_REFLECTION_DATA);
  if (FAILED(result)) {
    return BackendStatus{hresult_text("IDxcContainerBuilder::RemovePart", result),
                         shaders::ExitCode::CompileError};
  }
  ComPtr<IDxcOperationResult> serialized;
  result = state_->container_builder->SerializeContainer(
      reinterpret_cast<IDxcOperationResult**>(serialized.put_void()));
  HRESULT status = S_OK;
  if (FAILED(result) || serialized.get() == nullptr || FAILED(serialized->GetStatus(&status)) ||
      FAILED(status)) {
    return BackendStatus{hresult_text("IDxcContainerBuilder::SerializeContainer", result),
                         shaders::ExitCode::CompileError};
  }
  ComPtr<IDxcBlob> stripped;
  result = serialized->GetResult(reinterpret_cast<IDxcBlob**>(stripped.put_void()));
  if (FAILED(result) || stripped.get() == nullptr) {
    return BackendStatus{hresult_text("IDxcOperationResult::GetResult", result),
                         shaders::ExitCode::CompileError};
  }
  container = copy_blob(stripped.get());
  return BackendStatus{};
}

} // namespace bw::shader_compiler::detail
