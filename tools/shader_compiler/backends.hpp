#ifndef BW_SHADER_COMPILER_BACKENDS_HPP
#define BW_SHADER_COMPILER_BACKENDS_HPP
#include "bw/rendering/shaders/compiler_options.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/shader_compiler/logic.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: the three toolchain back ends shader_compiler drives, each
// behind a narrow interface so that only its own translation unit includes
// the vendor headers: DXC (dxc_backend.cpp), SPIRV-Tools and SPIRV-Reflect
// (spirv_backend.cpp), the Metal Shader Converter (metal_backend.cpp).
// Private to shader_compiler; toolchain.cpp composes them.
//
// Contract: every operation returns a BackendStatus. A failed status carries
// the ExitCode toolchain.hpp assigns to that failure and the back end's own
// message text; a successful one leaves the outputs set. No operation reads
// the environment. Ownership: sessions own their library handle and every
// object created through it, and release the objects before the handle.
//
// Core guidelines read: E.27 (named status), R.1 (sessions own their
// resources), I.11. Performance guidelines read: COPY.4 and COPY.2 (sessions
// are move-only with noexcept moves).
namespace bw::shader_compiler::detail {

struct BackendStatus {
  std::string message{};
  shaders::ExitCode code{shaders::ExitCode::Success};
  [[nodiscard]] bool ok() const noexcept { return code == shaders::ExitCode::Success; }
};

// ---- DXC ----------------------------------------------------------------------

struct DxcCompileInput {
  std::string_view source{};
  std::span<const std::string> arguments{};
  // The source roots in order (compiler_options.hpp, --shader-root): an
  // include is read from the first that holds it.
  std::span<const std::string_view> shader_roots{};
  // Served from memory when DXC asks for this path; never read from disk and
  // never recorded as an include.
  std::string_view generated_path{};
  std::string_view generated_text{};
};
struct DxcCompileOutput {
  std::vector<std::byte> object{};
  // DXC_OUT_REFLECTION: present for the DXIL targets only.
  std::vector<std::byte> reflection{};
  // DXC_OUT_PDB: present when the arguments ask for symbols on a DXIL target.
  std::vector<std::byte> pdb{};
  // Files the include handler served from the source roots, first-inclusion
  // order, each once, each path relative to the root that held it.
  std::vector<IncludedFile> includes{};
  // The same files in the same order, each as that root joined to its
  // relative path.
  std::vector<std::string> include_paths{};
};

class DxcSession {
public:
  DxcSession(const DxcSession&) = delete;
  DxcSession& operator=(const DxcSession&) = delete;
  DxcSession(DxcSession&& other) noexcept;
  DxcSession& operator=(DxcSession&& other) noexcept;
  ~DxcSession();

  // Loads DXC from `library_path` and creates its compiler, utilities and
  // container builder. On failure the status is ToolchainUnavailable with
  // the loader's or DXC's text, and `session` is null.
  [[nodiscard]] static BackendStatus open(std::string_view library_path,
                                          std::unique_ptr<DxcSession>& session);

  // "dxcompiler <major>.<minor>(<commit count>-<commit hash>)", read from the
  // loaded library's IDxcVersionInfo2, the same facts `dxc --version` prints:
  // DXC's part of the compiler identity in the input hash (artifact.hpp).
  // ToolchainUnavailable when the library does not report them.
  [[nodiscard]] BackendStatus identity(std::string& out) const;

  // CompileError with DXC's diagnostics when the shader does not compile.
  [[nodiscard]] BackendStatus compile(const DxcCompileInput& input, DxcCompileOutput& out) const;

  // Reads DXC_OUT_REFLECTION of a DXIL compile into a BackendReflection:
  // constant buffers by (register, space), resources by register class and
  // space through placement(), vertex inputs from the input signature of a
  // vertex shader with system values skipped, thread group size of a compute
  // shader. ContractViolation names a binding outside the root layout.
  [[nodiscard]] BackendStatus reflect(std::span<const std::byte> reflection,
                                      shaders::ShaderStage stage, BackendReflection& out) const;

  // Removes the reflection part from a DXIL container, keeping every other
  // part (the embedded root signature included).
  [[nodiscard]] BackendStatus remove_reflection_part(std::vector<std::byte>& container) const;

private:
  struct State;
  explicit DxcSession(std::unique_ptr<State> state) noexcept;
  std::unique_ptr<State> state_;
};

// ---- SPIR-V -------------------------------------------------------------------

// CompileError with the validator's messages unless the module validates for
// the Vulkan 1.3 environment.
[[nodiscard]] BackendStatus validate_spirv(std::span<const std::byte> module);
// The module that ships: SPIRV-Tools' performance passes, then, when
// `strip_debug`, the strip-debug pass. CompileError with the optimizer's
// messages on failure.
[[nodiscard]] BackendStatus optimize_spirv(std::span<const std::byte> module, bool strip_debug,
                                           std::vector<std::byte>& out);
// SPIRV-Reflect's view of the module as a BackendReflection: accessed
// descriptor bindings by (set, binding), the push constant block's root
// constant words when read, the vertex inputs keeps_vertex_input keeps, by
// location with their HLSL semantics, thread group size of a compute shader.
// ContractViolation names a binding outside the root layout, a kept vertex
// input without an HLSL semantic, or vertex input locations that are not
// dense from 0. CompileError when SPIRV-Reflect cannot read the module.
[[nodiscard]] BackendStatus reflect_spirv(std::span<const std::byte> module,
                                          shaders::ShaderStage stage, BackendReflection& out);

// ---- Metal Shader Converter -------------------------------------------------------

class MetalConverterSession {
public:
  MetalConverterSession(const MetalConverterSession&) = delete;
  MetalConverterSession& operator=(const MetalConverterSession&) = delete;
  MetalConverterSession(MetalConverterSession&& other) noexcept;
  MetalConverterSession& operator=(MetalConverterSession&& other) noexcept;
  ~MetalConverterSession();

  // Loads the converter from `library_path`, resolves its entry points and
  // reads the loaded image's LC_UUID for identity(). ToolchainUnavailable
  // when the library does not load, an entry point is missing, or the
  // loaded image carries no LC_UUID.
  [[nodiscard]] static BackendStatus open(std::string_view library_path,
                                          std::unique_ptr<MetalConverterSession>& session);

  // "metal-shaderconverter uuid:<32 hex digits>" (logic.hpp,
  // converter_identity) from the loaded library's LC_UUID: the converter's
  // part of the compiler identity in the input hash. The converter reports
  // no version at run time; the linker's UUID changes with every build of
  // the library.
  [[nodiscard]] const std::string& identity() const noexcept;

  // Converts one DXIL container to a Metal library. CompileError with the
  // converter's text on failure.
  [[nodiscard]] BackendStatus convert(std::span<const std::byte> dxil, std::string_view entry_point,
                                      shaders::ShaderStage stage,
                                      std::vector<std::byte>& out) const;

private:
  struct State;
  explicit MetalConverterSession(std::unique_ptr<State> state) noexcept;
  std::unique_ptr<State> state_;
};

} // namespace bw::shader_compiler::detail
#endif
