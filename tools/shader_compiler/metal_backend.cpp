// The Metal Shader Converter loaded from an explicit path. Its C header
// declares the entry points; the tool resolves each by name from the loaded
// library instead of linking it, so a missing or broken converter is a named
// ToolchainUnavailable failure with the loader's text.
#include "backends.hpp"
#include "shared_library.hpp"

#include <metal_irconverter/metal_irconverter.h>

#include <dlfcn.h>
#include <mach-o/loader.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Responsibility: the converter session of backends.hpp: DXIL in, a Metal
// library out, in process.
//
// Contract: as backends.hpp states. Every converter object (compiler, input
// and output objects, library binary, error) is destroyed on every path, and
// before the library handle closes.
//
// Algorithm, following Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Developer/Apple/MetalShaderFormat/Private/MetalCompileShaderMSC.cpp:
//   1. IRObjectCreateFromDXIL with a copy of the bytes (:1270).
//   2. IRCompilerCreate (:1272); compatibility flags bounds check, position
//      invariance, sample NaN to zero and texture write rounding toward zero
//      (:1275); minimum GPU family Metal3 (:1276); minimum deployment target
//      macOS 15.0.0 (:1277).
//   3. IRCompilerAllocCompileAndLink with the entry point name (:1312-1316).
//   4. IRMetalLibBinaryCreate, IRObjectGetMetalLibBinary for the stage,
//      IRMetalLibGetBytecodeSize, IRMetalLibGetBytecode (:888-903).
// The argument buffer layout comes from the root signature DXC embedded in
// the DXIL (compiler_options.hpp, -rootsig-define): the converter reads an
// embedded root signature unless told to ignore it
// (IRCompilerIgnoreRootSignature), so no IRRootSignature is built here.
// Departure: the pin builds its root signature in C++ and sets it with
// IRCompilerSetGlobalRootSignature (:1215-1232, :1273); here the one root
// signature text of root_layout.hpp reaches the converter inside the DXIL,
// so Direct3D 12 and Metal cannot see different layouts.
// Omitted:
//   - IRCompilerSetStageInGenerationMode(UseSeparateStageInFunction) (:1274):
//     the default, Metal vertex fetch, takes the input layout from the
//     pipeline's vertex descriptor; the separate stage-in function is
//     synthesized per input layout by the pin's RHI, and input layouts belong
//     to the RHI slice.
//   - IRCompilerSetEntryPointName with the CRC-unique name (:1315): the
//     pin renames to keep entry names unique inside its native shader
//     library archives; artifacts here hold one library each, so the
//     converter's own name for the entry is kept.
//   - The ray tracing, geometry emulation and dual source blending setup
//     (:1197-1210, :1279-1310): the game builds vertex, pixel and compute
//     shaders only.
//
// Retrieved rules: R.1 and E.6 (owning wrappers for every converter object),
// I.11, E.27. Performance: COPY.4, COPY.2.
namespace bw::shader_compiler::detail {
namespace {

// The converter entry points the session calls, resolved by name.
struct ConverterApi {
  decltype(&IRCompilerCreate) compiler_create{};
  decltype(&IRCompilerDestroy) compiler_destroy{};
  decltype(&IRCompilerSetCompatibilityFlags) set_compatibility_flags{};
  decltype(&IRCompilerSetMinimumGPUFamily) set_minimum_gpu_family{};
  decltype(&IRCompilerSetMinimumDeploymentTarget) set_minimum_deployment_target{};
  decltype(&IRCompilerAllocCompileAndLink) alloc_compile_and_link{};
  decltype(&IRObjectCreateFromDXIL) object_create_from_dxil{};
  decltype(&IRObjectDestroy) object_destroy{};
  decltype(&IRObjectGetMetalLibBinary) object_get_metal_lib_binary{};
  decltype(&IRMetalLibBinaryCreate) metal_lib_create{};
  decltype(&IRMetalLibBinaryDestroy) metal_lib_destroy{};
  decltype(&IRMetalLibGetBytecodeSize) metal_lib_size{};
  decltype(&IRMetalLibGetBytecode) metal_lib_bytes{};
  decltype(&IRErrorGetCode) error_code{};
  decltype(&IRErrorGetPayload) error_payload{};
  decltype(&IRErrorDestroy) error_destroy{};
};

template <class Function>
bool resolve(const SharedLibrary& library, const char* name, Function& out, std::string& error) {
  void* address = library.symbol(name, error);
  out = reinterpret_cast<Function>(address);
  return address != nullptr;
}

// Owns one converter object through its destroy function.
template <class T, class Destroy> class ConverterHandle {
public:
  ConverterHandle(T* object, Destroy destroy) noexcept : object_(object), destroy_(destroy) {}
  ConverterHandle(const ConverterHandle&) = delete;
  ConverterHandle& operator=(const ConverterHandle&) = delete;
  ConverterHandle(ConverterHandle&&) = delete;
  ConverterHandle& operator=(ConverterHandle&&) = delete;
  ~ConverterHandle() {
    if (object_ != nullptr) {
      destroy_(object_);
    }
  }
  [[nodiscard]] T* get() const noexcept { return object_; }

private:
  T* object_{};
  Destroy destroy_{};
};

IRShaderStage converter_stage(shaders::ShaderStage stage) noexcept {
  switch (stage) {
  case shaders::ShaderStage::Vertex:
    return IRShaderStageVertex;
  case shaders::ShaderStage::Pixel:
    return IRShaderStageFragment;
  case shaders::ShaderStage::Compute:
    return IRShaderStageCompute;
  }
  return IRShaderStageInvalid;
}

} // namespace

struct MetalConverterSession::State {
  // Declared first so it is destroyed last.
  SharedLibrary library{};
  ConverterApi api{};
  std::string identity{};
};

namespace {

// The LC_UUID of the loaded image that defines `symbol`. dladdr gives the
// image's base, where the loader mapped its Mach-O header and load commands;
// the header's sizeofcmds bounds the span handed to logic.hpp's mach_o_uuid,
// which walks it. Public <mach-o/loader.h> structures are used rather than
// _dyld_get_image_uuid, which the macOS SDK declares only in its private dyld
// headers; both read the same load command.
std::optional<std::array<std::uint8_t, 16>> loaded_image_uuid(const void* symbol) {
  Dl_info info{};
  if (symbol == nullptr || dladdr(symbol, &info) == 0 || info.dli_fbase == nullptr) {
    return std::nullopt;
  }
  const auto* header = static_cast<const mach_header_64*>(info.dli_fbase);
  if (header->magic != MH_MAGIC_64) {
    return std::nullopt;
  }
  const std::span<const std::byte> image{static_cast<const std::byte*>(info.dli_fbase),
                                         sizeof(mach_header_64) + header->sizeofcmds};
  return mach_o_uuid(image);
}

} // namespace

MetalConverterSession::MetalConverterSession(std::unique_ptr<State> state) noexcept
    : state_(std::move(state)) {}
MetalConverterSession::MetalConverterSession(MetalConverterSession&& other) noexcept = default;
MetalConverterSession&
MetalConverterSession::operator=(MetalConverterSession&& other) noexcept = default;
MetalConverterSession::~MetalConverterSession() = default;

BackendStatus MetalConverterSession::open(std::string_view library_path,
                                          std::unique_ptr<MetalConverterSession>& session) {
  session.reset();
  auto state = std::make_unique<State>();
  std::string error;
  state->library = SharedLibrary::open(library_path, error);
  if (!state->library.loaded()) {
    return BackendStatus{"cannot load the Metal Shader Converter from '" +
                             std::string{library_path} + "': " + error,
                         shaders::ExitCode::ToolchainUnavailable};
  }
  ConverterApi& api = state->api;
  const SharedLibrary& library = state->library;
  const bool resolved =
      resolve(library, "IRCompilerCreate", api.compiler_create, error) &&
      resolve(library, "IRCompilerDestroy", api.compiler_destroy, error) &&
      resolve(library, "IRCompilerSetCompatibilityFlags", api.set_compatibility_flags, error) &&
      resolve(library, "IRCompilerSetMinimumGPUFamily", api.set_minimum_gpu_family, error) &&
      resolve(library, "IRCompilerSetMinimumDeploymentTarget", api.set_minimum_deployment_target,
              error) &&
      resolve(library, "IRCompilerAllocCompileAndLink", api.alloc_compile_and_link, error) &&
      resolve(library, "IRObjectCreateFromDXIL", api.object_create_from_dxil, error) &&
      resolve(library, "IRObjectDestroy", api.object_destroy, error) &&
      resolve(library, "IRObjectGetMetalLibBinary", api.object_get_metal_lib_binary, error) &&
      resolve(library, "IRMetalLibBinaryCreate", api.metal_lib_create, error) &&
      resolve(library, "IRMetalLibBinaryDestroy", api.metal_lib_destroy, error) &&
      resolve(library, "IRMetalLibGetBytecodeSize", api.metal_lib_size, error) &&
      resolve(library, "IRMetalLibGetBytecode", api.metal_lib_bytes, error) &&
      resolve(library, "IRErrorGetCode", api.error_code, error) &&
      resolve(library, "IRErrorGetPayload", api.error_payload, error) &&
      resolve(library, "IRErrorDestroy", api.error_destroy, error);
  if (!resolved) {
    return BackendStatus{"Metal Shader Converter entry point missing: " + error,
                         shaders::ExitCode::ToolchainUnavailable};
  }
  const std::optional<std::array<std::uint8_t, 16>> uuid =
      loaded_image_uuid(library.symbol("IRCompilerCreate", error));
  if (!uuid.has_value()) {
    return BackendStatus{"the Metal Shader Converter loaded from '" + std::string{library_path} +
                             "' has no LC_UUID to identify it",
                         shaders::ExitCode::ToolchainUnavailable};
  }
  state->identity = converter_identity(*uuid);
  session.reset(new MetalConverterSession(std::move(state)));
  return BackendStatus{};
}

const std::string& MetalConverterSession::identity() const noexcept { return state_->identity; }

BackendStatus MetalConverterSession::convert(std::span<const std::byte> dxil,
                                             std::string_view entry_point,
                                             shaders::ShaderStage stage,
                                             std::vector<std::byte>& out) const {
  out.clear();
  const ConverterApi& api = state_->api;
  // Step 1.
  const ConverterHandle input(
      api.object_create_from_dxil(reinterpret_cast<const std::uint8_t*>(dxil.data()), dxil.size(),
                                  IRBytecodeOwnershipCopy),
      api.object_destroy);
  if (input.get() == nullptr) {
    return BackendStatus{"IRObjectCreateFromDXIL rejected the DXIL container",
                         shaders::ExitCode::CompileError};
  }
  // Step 2.
  const ConverterHandle compiler(api.compiler_create(), api.compiler_destroy);
  if (compiler.get() == nullptr) {
    return BackendStatus{"IRCompilerCreate returned null", shaders::ExitCode::ToolchainUnavailable};
  }
  constexpr unsigned kCompatibility = static_cast<unsigned>(IRCompatibilityFlagBoundsCheck) |
                                      static_cast<unsigned>(IRCompatibilityFlagPositionInvariance) |
                                      static_cast<unsigned>(IRCompatibilityFlagSampleNanToZero) |
                                      static_cast<unsigned>(IRCompatibilityFlagTexWriteRoundingRTZ);
  // The converter's C API takes OR-ed flags in its enum type, as the pin
  // passes them (:1275); the combination is not itself an enumerator.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  const auto compatibility = static_cast<IRCompatibilityFlags>(kCompatibility);
  api.set_compatibility_flags(compiler.get(), compatibility);
  api.set_minimum_gpu_family(compiler.get(), IRGPUFamilyMetal3);
  api.set_minimum_deployment_target(compiler.get(), IROperatingSystem_macOS, "15.0.0");

  // Step 3.
  const std::string entry{entry_point};
  IRError* raw_error = nullptr;
  const ConverterHandle output(
      api.alloc_compile_and_link(compiler.get(), entry.c_str(), input.get(), &raw_error),
      api.object_destroy);
  const ConverterHandle error(raw_error, api.error_destroy);
  if (output.get() == nullptr || error.get() != nullptr) {
    std::string message = "IRCompilerAllocCompileAndLink failed";
    if (error.get() != nullptr) {
      message += " (error code " + std::to_string(api.error_code(error.get())) + ")";
      const auto* payload = static_cast<const char*>(api.error_payload(error.get()));
      if (payload != nullptr) {
        message += ": ";
        message += payload;
      }
    }
    return BackendStatus{message, shaders::ExitCode::CompileError};
  }

  // Step 4.
  const ConverterHandle library(api.metal_lib_create(), api.metal_lib_destroy);
  if (library.get() == nullptr) {
    return BackendStatus{"IRMetalLibBinaryCreate returned null",
                         shaders::ExitCode::ToolchainUnavailable};
  }
  if (!api.object_get_metal_lib_binary(output.get(), converter_stage(stage), library.get())) {
    return BackendStatus{"the converter produced no Metal library for the shader's stage",
                         shaders::ExitCode::CompileError};
  }
  const std::size_t size = api.metal_lib_size(library.get());
  out.resize(size);
  const std::size_t written =
      api.metal_lib_bytes(library.get(), reinterpret_cast<std::uint8_t*>(out.data()));
  if (size == 0U || written != size) {
    out.clear();
    return BackendStatus{"the converter's Metal library is empty or was not copied whole",
                         shaders::ExitCode::CompileError};
  }
  return BackendStatus{};
}

} // namespace bw::shader_compiler::detail
