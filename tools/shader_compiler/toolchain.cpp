#include "bw/shader_compiler/toolchain.hpp"

#include "backends.hpp"
#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/compiler_options.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/shaders/root_layout.hpp"
#include "bw/shader_compiler/logic.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Responsibility: compile_shader of bw/shader_compiler/toolchain.hpp, the
// build-time algorithm of compiler_options.hpp composed from logic.hpp's pure
// decisions and backends.hpp's toolchain sessions.
//
// Contract: as toolchain.hpp states. Each failure returns at the step that
// found it with the status and text of that step; nothing after a failure
// runs, and no partial artifact is returned as a success.
//
// Algorithm (compiler_options.hpp, "Build-time algorithm"):
//   1. Check the permutation is in the shader's build set; take the defines.
//   2. Generate the include text from the shader's blocks and the engine
//      root layout; take the root signature text of that layout.
//   3. Read the main source from the first source root; build the DXC
//      argument list.
//   4. Open DXC (and the converter for Metal) and take their identities.
//   5. Compile; the include handler records every file it serves and the
//      root it came from.
//   6. Per target: DXIL targets reflect DXC's reflection blob and remove the
//      reflection part from the container; Metal converts that container;
//      Vulkan reflects the module as compiled, runs SPIRV-Tools' performance
//      passes (and strip-debug when symbols were asked for) on the module
//      that ships, and validates what the artifact will hold.
//   7. Assemble the backend-neutral Reflection, hash the inputs, write the
//      artifact image with the compiler identity in the trailer.
// The Features trailer entry (artifact.hpp, ShaderFeature) is not written.
// Nothing consumes it yet, and deriving it from DXIL feature flags and
// SPIR-V capabilities is work for the slice that reads it.
//
// Retrieved rules: E.27 (one status per failure, returned at once), I.1
// (every path from the request), R.1 (sessions own the toolchain objects).
// Performance: COPY.1 (the product is built in place and returned).
namespace bw::shader_compiler {
namespace {

using detail::BackendStatus;

CompileProduct fail(CompileProduct& product, shaders::ExitCode code, std::string message) {
  product.status = code;
  product.diagnostics = std::move(message);
  product.artifact.clear();
  product.symbols.clear();
  return std::move(product);
}

CompileProduct fail(CompileProduct& product, const BackendStatus& status) {
  return fail(product, status.code, status.message);
}

bool read_file(const std::string& path, std::string& out) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return false;
  }
  out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
  return !stream.bad();
}

// The comma-separated source roots in order (compiler_options.hpp,
// --shader-root); an empty entry is kept so the caller can refuse it.
std::vector<std::string_view> split_roots(std::string_view list) {
  std::vector<std::string_view> roots;
  std::size_t begin = 0;
  while (true) {
    const std::size_t comma = list.find(',', begin);
    roots.push_back(list.substr(begin, comma == std::string_view::npos ? comma : comma - begin));
    if (comma == std::string_view::npos) {
      return roots;
    }
    begin = comma + 1;
  }
}

std::string_view include_error_name(IncludeError error) noexcept {
  switch (error) {
  case IncludeError::None:
    return "None";
  case IncludeError::NullBlock:
    return "NullBlock";
  case IncludeError::StaticBlockWithoutSlot:
    return "StaticBlockWithoutSlot";
  case IncludeError::StaticSlotHashMismatch:
    return "StaticSlotHashMismatch";
  case IncludeError::SharedBlock:
    return "SharedBlock";
  case IncludeError::Hlsl:
    return "Hlsl";
  }
  return "unknown";
}

std::string_view write_error_name(shaders::ArtifactWriteError error) noexcept {
  using shaders::ArtifactWriteError;
  switch (error) {
  case ArtifactWriteError::None:
    return "None";
  case ArtifactWriteError::InvalidTargetOrStage:
    return "InvalidTargetOrStage";
  case ArtifactWriteError::StringTooLong:
    return "StringTooLong";
  case ArtifactWriteError::TooManyBlocks:
    return "TooManyBlocks";
  case ArtifactWriteError::TooManyVertexInputs:
    return "TooManyVertexInputs";
  case ArtifactWriteError::SlotOutOfRange:
    return "SlotOutOfRange";
  case ArtifactWriteError::DuplicateSlot:
    return "DuplicateSlot";
  case ArtifactWriteError::ThreadGroupOnNonCompute:
    return "ThreadGroupOnNonCompute";
  case ArtifactWriteError::UnknownDescriptorArray:
    return "UnknownDescriptorArray";
  case ArtifactWriteError::EmptyCode:
    return "EmptyCode";
  case ArtifactWriteError::TrailerEntryTooLarge:
    return "TrailerEntryTooLarge";
  case ArtifactWriteError::ImageTooLarge:
    return "ImageTooLarge";
  }
  return "unknown";
}

std::string_view reflection_error_name(ReflectionError error) noexcept {
  switch (error) {
  case ReflectionError::None:
    return "None";
  case ReflectionError::StaticBlockNotDeclared:
    return "StaticBlockNotDeclared";
  case ReflectionError::DrawBlockNotDeclared:
    return "DrawBlockNotDeclared";
  case ReflectionError::DuplicateBlock:
    return "DuplicateBlock";
  case ReflectionError::TooManyBlocks:
    return "TooManyBlocks";
  case ReflectionError::TooManyVertexInputs:
    return "TooManyVertexInputs";
  case ReflectionError::SemanticTooLong:
    return "SemanticTooLong";
  }
  return "unknown";
}

} // namespace

CompileProduct compile_shader(const CompileRequest& request) {
  CompileProduct product{};
  if (request.shader == nullptr) {
    return fail(product, shaders::ExitCode::ContractViolation, "no shader description given");
  }
  const shaders::ShaderDescription& shader = *request.shader;
  const shaders::RootLayout& layout = shaders::engine_root_layout();

  // Step 1.
  std::vector<shaders::PermutationId> build_set(shader.permutation_count);
  build_set.resize(shaders::enumerate_permutations(shader, build_set));
  bool in_build_set = false;
  for (const shaders::PermutationId id : build_set) {
    in_build_set = in_build_set || id == request.permutation;
  }
  if (!in_build_set) {
    return fail(product, shaders::ExitCode::ContractViolation,
                "permutation " + std::to_string(request.permutation.value) +
                    " is not in the build set of shader '" + std::string{shader.name} + "'");
  }
  std::array<shaders::Define, shaders::kMaxDimensions> define_storage{};
  const std::size_t define_count = shader.emit_defines != nullptr
                                       ? shader.emit_defines(request.permutation, define_storage)
                                       : 0U;
  if (define_count > define_storage.size()) {
    return fail(product, shaders::ExitCode::ContractViolation,
                "shader declares more permutation dimensions than kMaxDimensions");
  }
  const std::span<const shaders::Define> defines{define_storage.data(), define_count};

  // Step 2.
  std::string generated;
  const IncludeResult include = append_generated_include(shader, layout, generated);
  if (include.error != IncludeError::None) {
    return fail(product, shaders::ExitCode::ContractViolation,
                "generated include for '" + std::string{shader.name} +
                    "': " + std::string{include_error_name(include.error)} + " at '" +
                    std::string{include.block} + "'");
  }
  const std::string generated_path = generated_include_path(shader.name);
  std::string root_signature;
  shaders::append_root_signature_text(layout, root_signature);

  // Step 3. The main source is the first root's. An empty entry would
  // resolve against the working directory, so it is a bad invocation.
  const std::vector<std::string_view> roots = split_roots(request.shader_root);
  if (std::find(roots.begin(), roots.end(), std::string_view{}) != roots.end()) {
    return fail(product, shaders::ExitCode::UsageError,
                "--shader-root lists an empty source root: '" + std::string{request.shader_root} +
                    "'");
  }
  const std::string source_file = join_path(roots.front(), shader.source_path);
  std::string source;
  if (!read_file(source_file, source)) {
    return fail(product, shaders::ExitCode::CompileError,
                "cannot read shader source '" + source_file + "'");
  }
  std::vector<std::string> arguments;
  shaders::append_compiler_arguments(request.target, shader.stage, shader.entry_point, defines,
                                     root_signature, request.emit_symbols, arguments);

  // Step 4.
  std::unique_ptr<detail::DxcSession> dxc;
  BackendStatus status = detail::DxcSession::open(request.dxc_library, dxc);
  if (!status.ok()) {
    return fail(product, status);
  }
  std::string identity;
  status = dxc->identity(identity);
  if (!status.ok()) {
    return fail(product, status);
  }
  std::unique_ptr<detail::MetalConverterSession> converter;
  if (request.target == shaders::ShaderTarget::Metal) {
    if (request.converter_library.empty()) {
      return fail(product, shaders::ExitCode::ContractViolation,
                  "a Metal compile needs the converter library path");
    }
    status = detail::MetalConverterSession::open(request.converter_library, converter);
    if (!status.ok()) {
      return fail(product, status);
    }
    identity += "; ";
    identity += converter->identity();
  }

  // Step 5.
  detail::DxcCompileOutput compiled{};
  status = dxc->compile(
      detail::DxcCompileInput{source, arguments, roots, generated_path, generated}, compiled);
  if (!status.ok()) {
    // The argument list carries no source name (compiler_options.hpp), so
    // DXC calls the main source hlsl.hlsl in its messages; say which file
    // that is.
    status.message += "\n(hlsl.hlsl in DXC's messages is ";
    status.message += source_file;
    status.message += ')';
    return fail(product, status);
  }

  // Step 6.
  std::vector<std::byte> code;
  if (request.target == shaders::ShaderTarget::Vulkan) {
    status = detail::reflect_spirv(compiled.object, shader.stage, product.reflection);
    if (!status.ok()) {
      return fail(product, status);
    }
    if (request.emit_symbols) {
      product.symbols = compiled.object;
    }
    status = detail::optimize_spirv(compiled.object, request.emit_symbols, code);
    if (!status.ok()) {
      return fail(product, status);
    }
    status = detail::validate_spirv(code);
    if (!status.ok()) {
      return fail(product, status);
    }
  } else {
    status = dxc->reflect(compiled.reflection, shader.stage, product.reflection);
    if (!status.ok()) {
      return fail(product, status);
    }
    code = std::move(compiled.object);
    status = dxc->remove_reflection_part(code);
    if (!status.ok()) {
      return fail(product, status);
    }
    if (request.emit_symbols) {
      if (compiled.pdb.empty()) {
        return fail(product, shaders::ExitCode::CompileError,
                    "symbols were asked for and DXC produced no PDB");
      }
      product.symbols = std::move(compiled.pdb);
    }
    if (request.target == shaders::ShaderTarget::Metal) {
      std::vector<std::byte> library;
      status = converter->convert(code, shader.entry_point, shader.stage, library);
      if (!status.ok()) {
        return fail(product, status);
      }
      code = std::move(library);
    }
  }

  // Step 7.
  const AssembledReflection assembled = assemble_reflection(product.reflection, shader, layout);
  if (assembled.error != ReflectionError::None) {
    return fail(product, shaders::ExitCode::ContractViolation,
                "reflection of '" + std::string{shader.name} +
                    "': " + std::string{reflection_error_name(assembled.error)} + " (slot " +
                    std::to_string(assembled.slot) + ")");
  }
  for (const IncludedFile& file : compiled.includes) {
    product.includes.push_back(file.path);
  }
  product.include_paths = compiled.include_paths;
  product.input_hash = compute_input_hash(HashInputs{
      .compiler_identity = identity,
      .entry_point = shader.entry_point,
      .defines = defines,
      .arguments = arguments,
      .generated_include = generated,
      .main_source = source,
      .includes = compiled.includes,
      .root_layout_hash = shaders::root_layout_hash(layout),
      .target = request.target,
      .stage = shader.stage,
  });
  const shaders::ArtifactHeader header{
      .shader_name = shader.name,
      .entry_point = shader.entry_point,
      .input_hash = product.input_hash,
      .root_layout_hash = shaders::root_layout_hash(layout),
      .permutation = request.permutation,
      .target = request.target,
      .stage = shader.stage,
  };
  const std::array trailer{shaders::TrailerEntry{
      .bytes = std::as_bytes(std::span{identity.data(), identity.size()}),
      .key = shaders::TrailerKey::CompilerIdentity,
  }};
  const shaders::ArtifactWriteError written =
      shaders::write_artifact(header, assembled.reflection, code, trailer, product.artifact);
  if (written != shaders::ArtifactWriteError::None) {
    return fail(product, shaders::ExitCode::ContractViolation,
                "the artifact writer refused the inputs: ArtifactWriteError::" +
                    std::string{write_error_name(written)});
  }
  product.status = shaders::ExitCode::Success;
  return product;
}

} // namespace bw::shader_compiler
