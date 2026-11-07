#ifndef BW_SHADER_COMPILER_TOOLCHAIN_HPP
#define BW_SHADER_COMPILER_TOOLCHAIN_HPP
#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/compiler_options.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/shaders/root_layout.hpp"
#include "bw/shader_compiler/logic.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: the toolchain-facing half of shader_compiler. One call
// turns a registry shader, a permutation and a target into the artifact
// bytes, the symbol bytes and the list of files it read, through DXC (loaded
// from a path), SPIRV-Tools and SPIRV-Reflect (linked) and, for Metal, the
// Metal Shader Converter (loaded from a path). The executable
// (tools/shader_compiler/main.cpp) writes the files; the integration tests
// call this directly to read the backend reflection the artifact does not
// keep. docs/architecture/shaders.md, "Build-time flow";
// compiler_options.hpp, "Build-time algorithm".
//
// Contract. Inputs: the request below; every path is the caller's, nothing
// is read from the environment. Output: a CompileProduct whose status is
// one of compiler_options.hpp's ExitCode values:
//   Success               artifact, includes and reflection are set; symbols
//                         are set when emit_symbols;
//   ToolchainUnavailable  a library path has no '/', a library or its file
//                         could not be read or loaded, a symbol was
//                         missing, a DXC object could not be created, DXC
//                         did not report its version, or the loaded
//                         converter image carries no LC_UUID; diagnostics
//                         holds the loader's or DXC's text or the refusal;
//   CompileError          DXC (including any message in its error buffer on
//                         a successful status), SPIR-V optimization or
//                         validation, or the converter rejected the shader;
//                         a source file could not be read; or a backend's
//                         reflection could not be read whole; diagnostics
//                         holds their messages;
//   ContractViolation     no shader, a permutation outside the build set, a
//                         Metal request with no converter_library (a
//                         programming error once parse_command_line has
//                         checked the command line), a binding outside the
//                         root layout or non-dense vertex input locations
//                         (backends), logic.hpp's named errors, or the
//                         artifact writer's ArtifactWriteError, each by
//                         name in diagnostics.
// Compiler identity, the first variable item of the input hash
// (artifact.hpp, InputHash), describes the libraries actually loaded:
// "dxcompiler <major>.<minor>(<commit count>-<commit hash>)" from DXC's
// IDxcVersionInfo2 (version and commit, the facts `dxc --version` prints),
// and for Metal "; " then "metal-shaderconverter uuid:<32 hex digits>" from
// the converter library's LC_UUID, read from the image the loader mapped
// (logic.hpp, mach_o_uuid and converter_identity; an image without one is
// ToolchainUnavailable, above, with no fallback). So the input hash changes when either
// library changes, no header constant enters it, and no library bytes are
// read beyond loading. The same text is every artifact's CompilerIdentity
// trailer entry.
// The Metal library's function is named after the shader's entry point: the
// converter keeps the name, so the RHI finds the function by
// ShaderDescription::entry_point.
// The same request on the same host gives byte-identical products
// (docs/architecture/shaders.md, invariant 3): no timestamps, no absolute
// paths in any compiler argument, sorted reflection. Ownership: the product
// owns everything it holds. No synchronization: one call uses its own
// library handles and compiler objects and releases them before returning.
//
// Core guidelines read: R.1 and E.6 (every library handle and COM-style
// object is held by an owning handle released on every path), E.27 (a
// named status, never a bool), I.1 (paths are parameters), I.11 (no
// ownership passes through a raw pointer), R.31 (the reference-counted
// handle follows unique_ptr's pattern: move-only, released in its
// destructor). Performance guidelines read: COPY.4 (owning types follow the
// rule of zero or declare all five), COPY.2 (their moves are noexcept),
// COPY.1 (products returned by value, no std::move of locals).
namespace bw::shader_compiler {

struct CompileRequest {
  // A registry entry (shader_registry()); not owned.
  const shaders::ShaderDescription* shader{};
  // The source roots, comma separated (compiler_options.hpp, --shader-root):
  // the main source is read from the first; an include from the first
  // that holds it.
  std::string_view shader_root{};
  std::string_view dxc_library{};
  // Required for ShaderTarget::Metal, ignored otherwise.
  std::string_view converter_library{};
  shaders::PermutationId permutation{};
  shaders::ShaderTarget target{shaders::ShaderTarget::Direct3D12};
  bool emit_symbols{};
};

struct CompileProduct {
  std::vector<std::byte> artifact{};
  // DXC's PDB for the DXIL targets; the SPIR-V module as compiled, before
  // optimization and debug strip, for Vulkan. Empty unless emit_symbols.
  std::vector<std::byte> symbols{};
  // Every file the compile read besides the main source, relative to the
  // source root that holds it, in first-inclusion order. The generated
  // include is not a file and is not listed.
  std::vector<std::string> includes{};
  // The same files in the same order, each as its root joined to the
  // relative path: what a dependency file names.
  std::vector<std::string> include_paths{};
  // What the backend's own reflection reported, member offsets included:
  // DXC's for the DXIL targets, SPIRV-Reflect's for Vulkan.
  BackendReflection reflection{};
  std::string diagnostics{};
  shaders::InputHash input_hash{};
  shaders::ExitCode status{shaders::ExitCode::Success};
};

[[nodiscard]] CompileProduct compile_shader(const CompileRequest& request);

} // namespace bw::shader_compiler
#endif
