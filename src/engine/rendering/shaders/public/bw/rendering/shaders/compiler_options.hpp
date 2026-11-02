#ifndef BW_RENDERING_SHADERS_COMPILER_OPTIONS_HPP
#define BW_RENDERING_SHADERS_COMPILER_OPTIONS_HPP
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/shaders/root_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: the shader_compiler command line and the fixed compiler
// argument set, as one contract the tool, CMake and the tests share
// (docs/architecture/shaders.md, "Build-time flow"). The tool has two modes:
// `enumerate` prints the finite artifact set so CMake can declare one command
// per artifact; `compile` builds one artifact. Everything that decides output
// bytes is an explicit option or a constant here; nothing is read from the
// environment.
//
// Contract. parse_command_line takes the arguments after the program name
// and returns a ToolInvocation or a named CommandLineError with the
// offending token. Errors are found in argument order and the first one is
// reported: an unknown option, a value missing (the next argument is absent
// or starts with "--"), a repeated option, a target that does not parse or
// repeats or exceeds three, a permutation that does not parse, or an
// argument where an option was expected; after the walk, a required option
// missing (offending is that option's name, the one case where it is not a
// view into the arguments), then a Metal compile without a converter
// library. Each is a test. Invocation string views point into the argument
// strings. usage_text describes the same grammar. append_compiler_arguments
// appends the DXC arguments for a target, stage, entry point, define list
// and root signature text; no argument contains a host path, defines follow
// domain order, and the list is part of the artifact input hash
// (artifact.hpp). append_enumerate_line produces the line CMake parses. Exit
// codes are fixed below. Ownership: the invocation borrows the argument
// strings. No synchronization: pure functions.
//
// Grammar:
//   shader_compiler enumerate --targets <t>[,<t>...] --output-root <dir>
//   shader_compiler compile --shader <name> --permutation <id> --target <t>
//       --shader-root <dir> --output-root <dir> --depfile <path>
//       --dxc-library <path> [--converter-library <path>] [--symbols <dir>]
//   <t> is d3d12, vulkan or metal. --converter-library is required when
//   <t> is metal and ignored otherwise. --symbols enables symbol output.
//
// Build-time algorithm (compile mode, implemented by the tool):
//   1. Resolve the shader by name in the registry; fail with
//      ContractViolation if absent or if a static block has no root slot.
//      Take the root signature text from append_root_signature_text on the
//      engine root layout (root_layout.hpp).
//   2. Generate the include text for the shader's blocks (parameter_block.hpp)
//      and serve it to DXC through the include handler as
//      "bw/generated/<shader>.hlsli"; record every opened include.
//   3. Compile with append_compiler_arguments; on diagnostics, print them and
//      exit CompileError.
//   4. Per target: Direct3D 12 keeps the DXIL container with its reflection
//      part removed and the embedded root signature kept; Metal converts the
//      same DXIL in process with the converter, which lays out its argument
//      buffer from that embedded root signature; Vulkan runs the SPIR-V
//      through SPIRV-Tools and reflects it with SPIRV-Reflect.
//   5. Derive the Reflection from the compiler's reflection, write the
//      artifact, the symbol file (when --symbols) named by the input hash,
//      and the Makefile-format dependency file listing the main source and
//      every include opened.
//
// Design and cost: parsing is one pass over the arguments; the tool allocates
// freely (build time). Determinism is designed in: fixed arguments, no
// paths in arguments, no timestamps, sorted output.
//
// Core guidelines read: I.1 (every input is an option; no environment
// variable or default path), E.27 (named error enum and exit codes),
// Enum.3 (ToolMode, CommandLineError, ExitCode), SL.str.2 (views into the
// arguments), R.14 (spans). Performance guidelines read: EMB.6 and GDSA.17
// (the argument set is fixed data; the artifact is built once per input
// key, which the arguments are part of). A search of the performance corpus
// for command-line or build-tool rules returned no applicable rule;
// recorded here.
namespace bw::shaders {

enum class ToolMode : std::uint8_t {
  Enumerate,
  Compile,
};

struct TargetSet {
  std::array<ShaderTarget, 3> targets{};
  std::uint32_t count{};
  [[nodiscard]] std::span<const ShaderTarget> view() const noexcept {
    return {targets.data(), count};
  }
};

struct EnumerateOptions {
  TargetSet targets{};
  std::string_view output_root{};
};

struct CompileOptions {
  std::string_view shader{};
  std::string_view shader_root{};
  std::string_view output_root{};
  std::string_view depfile{};
  std::string_view dxc_library{};
  std::string_view converter_library{};
  std::string_view symbols_dir{};
  PermutationId permutation{};
  ShaderTarget target{ShaderTarget::Direct3D12};
};

struct ToolInvocation {
  EnumerateOptions enumerate{};
  CompileOptions compile{};
  ToolMode mode{ToolMode::Enumerate};
};

enum class CommandLineError : std::uint8_t {
  None,
  MissingMode,
  UnknownMode,
  UnknownOption,
  MissingValue,
  RepeatedOption,
  MissingRequiredOption,
  InvalidTarget,
  InvalidPermutation,
  ConverterRequiredForMetal,
  UnexpectedArgument,
};

// offending, per error: the argument itself for UnknownMode, UnknownOption,
// RepeatedOption and UnexpectedArgument; the option whose value is missing or
// does not parse for MissingValue, InvalidTarget and InvalidPermutation
// (the option argument, so a view into the arguments); empty for
// MissingMode; the option's name as static text for MissingRequiredOption and
// ConverterRequiredForMetal, the two errors found after the walk. A
// --converter-library given with a non-Metal target is parsed and kept in
// the invocation; the tool ignores it.
struct CommandLineResult {
  ToolInvocation invocation{};
  std::string_view offending{};
  CommandLineError error{CommandLineError::None};
  [[nodiscard]] bool ok() const noexcept { return error == CommandLineError::None; }
};

// args excludes the program name.
[[nodiscard]] CommandLineResult parse_command_line(std::span<const std::string_view> args) noexcept;
[[nodiscard]] std::string_view usage_text() noexcept;

// Process exit codes; main returns the value as int.
enum class ExitCode : std::uint8_t {
  Success = 0,
  UsageError = 1,
  CompileError = 2,
  ToolchainUnavailable = 3,
  OutputError = 4,
  ContractViolation = 5,
};

// Target names on the command line and in enumerate output: d3d12, vulkan,
// metal.
[[nodiscard]] std::string_view target_name(ShaderTarget target) noexcept;
[[nodiscard]] std::optional<ShaderTarget> parse_target(std::string_view name) noexcept;
// DXC profile for a stage: vs_6_4, ps_6_4, cs_6_4 (docs/architecture/shaders.md,
// "Targets": shader model 6.4 is the Direct3D 12 floor, the Xbox Dev Mode
// container's ceiling).
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Developer/Windows/ShaderFormatD3D/Private/D3DShaderCompiler.cpp:131-146:
// per-frequency `<stage>_6_<minor>` profile names for the SM6 format;
// Engine/Source/Developer/Apple/MetalShaderFormat/Private/MetalCompileShaderMSC.cpp:1019:
// one shader model for the converter path. Departure: minor version 4, not
// 6, so nothing in the shaders can require the shader model 6.6 descriptor
// heap or any later feature.
[[nodiscard]] std::string_view stage_profile(ShaderStage stage) noexcept;

// Enumerate output: one line per artifact,
//   "<shader>\t<permutation>\t<target name>\t<target dir>/<artifact file name>\n"
// in registry order, then permutation ascending, then target in the order
// given. Nothing else is printed on standard output.
void append_enumerate_line(const ShaderDescription& shader, PermutationId permutation,
                           ShaderTarget target, std::string& out);

// The DXC argument list, in this order:
//   -HV 2021  -Zpr  -O3  -WX  -enable-16bit-types
//   with symbols, DXIL targets: -Zss  -Zi  -Qstrip_debug
//   with symbols, Vulkan:       -Zi
//   -disable-lifetime-markers
//   -D BW_TARGET_D3D12=1 | -D BW_TARGET_VULKAN=1 | -D BW_TARGET_METAL=1
//   -D <name>=<value> for each define, in domain order
//   DXIL targets: -D BW_ROOT_SIGNATURE=<root_signature>
//                 -rootsig-define BW_ROOT_SIGNATURE
//   Vulkan only: -spirv  -fspv-target-env=vulkan1.3  -fvk-use-dx-layout
//                -fspv-reflect
//   with symbols, Vulkan:       -fspv-debug=line
//   -E <entry>  -T <profile>
// root_signature is the text from append_root_signature_text and is ignored
// on Vulkan. The pin's order is kept: the fixed flags, the symbol flags
// before -disable-lifetime-markers, then the defines, then the entry point
// and the profile last.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Developer/Windows/ShaderFormatD3D/Private/D3DShaderCompilerDXC.cpp:136-364
// (FDxcArguments): -HV 2021 at :180-181, -Zpr :190, -O3 :207, -WX :222,
// -enable-16bit-types :241, -Zss then -Zi then -Qstrip_debug :249-261,
// -disable-lifetime-markers :276; GetCompilerArgs appends -E :325 and -T
// :329 after the other arguments (:320-330).
// Engine/Source/ThirdParty/ShaderConductor/ShaderConductor/Source/Core/ShaderConductor.cpp:1254
// pushes -Zi when debug info is enabled on the SPIR-V path, before
// -fspv-debug=line at :1350; followed. Omitted from that range:
// -Wno-parentheses-equality and -Wno-for-redefinition (:264-266) and the
// bindless -Wno-ignored-attributes (:270-273), because the shaders here are
// new source expected to compile clean under -WX; -Vd (:192-195), because
// validation always runs; -exports (:231-237), no ray tracing.
// Engine/Source/ThirdParty/ShaderConductor/ShaderConductor/Source/Core/ShaderConductor.cpp:1233
// (-Zpr for every target), :1244 (-enable-16bit-types), :1333 (-spirv),
// :1342 (-fspv-reflect), :1350 (-fspv-debug=line).
// Engine/Source/Developer/VulkanShaderFormat/Private/VulkanShaderCompiler.cpp:88-90:
// Vulkan 1.3 for SM6.
// Departures, each because the game ships against upstream DXC and one
// bindless layout: -fvk-ue5-layout
// (Engine/Source/ThirdParty/ShaderConductor/ShaderConductor/Source/Core/ShaderConductor.cpp:1335)
// and -fvk-use-scalar-layout
// (Engine/Source/Developer/ShaderCompilerCommon/Private/ShaderConductorContext.cpp:434) are
// replaced by -fvk-use-dx-layout so constant buffer offsets equal the Direct3D ones and the
// parameter block padding holds on Vulkan; -auto-binding-space
// (Engine/Source/Developer/Windows/ShaderFormatD3D/Private/D3DShaderCompilerDXC.cpp:227) is not
// passed because the include binds explicit register spaces from root_layout.hpp;
// -fvk-use-dx-position-w
// (Engine/Source/ThirdParty/ShaderConductor/ShaderConductor/Source/Core/ShaderConductor.cpp:1339)
// is not passed because no Metal shader is built from SPIR-V. Omitted: -Od/-O1 selection (:199-203)
// and -Gfp/-Gfa
// (:212-217), one optimization level and default float semantics; the
// SPIR-V entry point rename to main_<size>_<crc>
// (Engine/Source/Developer/ShaderCompilerCommon/Private/SpirvCommon.cpp:154,175-190),
// the entry point keeps its declared name. Whether the chosen DXC tag
// accepts -fvk-use-dx-layout and -fspv-reflect is a fact the slice
// establishes; the integration test fails if it does not.
void append_compiler_arguments(ShaderTarget target, ShaderStage stage, std::string_view entry_point,
                               std::span<const Define> defines, std::string_view root_signature,
                               bool emit_symbols, std::vector<std::string>& out);

} // namespace bw::shaders
#endif
