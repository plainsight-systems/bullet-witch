#ifndef BW_SHADER_COMPILER_LOGIC_HPP
#define BW_SHADER_COMPILER_LOGIC_HPP
#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
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

// Responsibility: the pure decisions of shader_compiler, with no toolchain,
// process or file dependency, so each is a unit test (AGENTS.md, Testing:
// logic only reachable through an OS-bound path gets its own pure function).
// The toolchain half (toolchain.hpp) feeds these functions what DXC,
// SPIRV-Reflect and the converter report and acts on their answers.
//
// Contract, per function below:
//   - Binding classification maps what a backend reflects (DXIL register and
//     space, Vulkan set and binding) to the root layout element it must be,
//     using only root_layout.hpp's constants and placement(). Anything that
//     maps to no element is no value: the tool reports ContractViolation
//     (compiler_options.hpp, ExitCode), never a guess.
//   - assemble_reflection builds the artifact's backend-neutral Reflection
//     from one backend's report. Block identity is the slot a block binds
//     to, never the name the backend gives it, and names and layout hashes
//     come from the registry's BlockLayouts; so the same shader and
//     permutation give the same Reflection on every target.
//   - compute_input_hash is BLAKE3-256 over the inputs artifact.hpp lists for
//     InputHash, in that order, each item length-prefixed.
//   - append_generated_include, append_depfile and the name functions produce
//     exact text; each is a golden-text test.
// Failure: every function either cannot fail or returns a named error.
// Ownership: results that hold string_views say what they view. No
// synchronization: pure functions.
//
// Core guidelines read: E.27 (named error enums and result structs; no
// exceptions and no bool success flags), SL.str.2 (string_view for borrowed
// text, std::string where the function produces text), SL.con.3 (lengths are
// checked before every index), I.1 (the root layout and description are
// parameters; nothing is ambient), Enum.3 (class enums). Performance
// guidelines read: COPY.1 and COPY.8 (results returned by value as prvalues;
// no std::move of locals), COPY.7 (no copies in range-for over strings).
namespace bw::shader_compiler {

// ---- binding classification ------------------------------------------------

// The root layout element a reflected constant buffer binds to.
enum class BlockBindingKind : std::uint8_t {
  Static,
  Draw,
  RootConstants,
};
struct BlockBinding {
  BlockBindingKind kind{BlockBindingKind::Static};
  // Static slot index for Static; 0 otherwise.
  std::uint32_t slot{};
  [[nodiscard]] friend constexpr bool operator==(const BlockBinding&,
                                                 const BlockBinding&) noexcept = default;
};

// DXIL targets (root_layout.hpp, RootElement table): register b<i> in
// kStaticBlockSpace with i below kMaxStaticBlocks is static slot i; b0 in
// kDrawBlockSpace is the draw block; b0 in kRootConstantSpace is the root
// constants. Anything else binds to nothing in the layout: no value.
[[nodiscard]] std::optional<BlockBinding>
classify_dxil_constant_buffer(std::uint32_t register_index, std::uint32_t space) noexcept;

// Vulkan: uniform buffers reached by index live in kVulkanUniformSet; binding
// i below kMaxStaticBlocks is static slot i and binding kMaxStaticBlocks is
// the draw block (the push constant indices of root_layout.hpp's table).
// Root constants are push constants on Vulkan, not a binding. Anything else:
// no value.
[[nodiscard]] std::optional<BlockBinding>
classify_vulkan_uniform_buffer(std::uint32_t set, std::uint32_t binding) noexcept;

// The descriptor array whose placement() has that register class ('s', 't'
// or 'u') and register space; no value when none has.
[[nodiscard]] std::optional<shaders::DescriptorArray>
descriptor_array_for_register(char register_class, std::uint32_t space) noexcept;

// The descriptor array whose placement() has that Vulkan set and binding; no
// value when none has.
[[nodiscard]] std::optional<shaders::DescriptorArray>
descriptor_array_for_vulkan_binding(std::uint32_t set, std::uint32_t binding) noexcept;

// An HLSL semantic split into name and index: trailing decimal digits are the
// index ("TEXCOORD1" is TEXCOORD, 1; "COLOR" is COLOR, 0), the name is
// upper-cased because HLSL semantics compare without case. An empty
// semantic, one made of digits only, or an index that does not fit 32 bits is
// no value.
struct Semantic {
  std::string name{};
  std::uint32_t index{};
  [[nodiscard]] friend bool operator==(const Semantic&, const Semantic&) = default;
};
[[nodiscard]] std::optional<Semantic> split_semantic(std::string_view semantic);

// Whether a vertex shader input is a vertex input the artifact records
// (artifact.hpp, VertexInput): it is not a system value. Both bytecode paths
// decide with this one predicate so they exclude the same inputs:
// `system_value` is SystemValueType != D3D_NAME_UNDEFINED in the DXIL input
// signature, or the BuiltIn decoration on a SPIR-V input; and independently
// of that flag, a semantic starting with "SV_" (any case; HLSL semantics
// compare without case, and DXC's -fspv-reflect carries the semantic into
// SPIR-V) is a system value.
[[nodiscard]] bool keeps_vertex_input(std::string_view semantic, bool system_value) noexcept;

// ---- reflection ---------------------------------------------------------------

// One member of a reflected constant buffer, at its byte offset.
struct ReflectedMember {
  std::string name{};
  std::uint32_t offset{};
};
// A constant buffer the shader reads, by the element it binds to, with the
// top-level members the backend reports (generated padding members included).
struct BoundBlock {
  BlockBinding binding{};
  std::vector<ReflectedMember> members{};
};
struct BackendVertexInput {
  Semantic semantic{};
  std::uint32_t location{};
};
// What one backend's reflection says the shader uses, already classified.
// blocks holds static and draw blocks only; root constants are the flag.
// vertex_inputs holds the inputs keeps_vertex_input keeps; location is the
// input's ordinal among those, which is the Vulkan location DXC assigns and
// the DXIL signature order with system values skipped.
struct BackendReflection {
  std::vector<BoundBlock> blocks{};
  std::vector<BackendVertexInput> vertex_inputs{};
  std::array<std::uint32_t, 3> thread_group_size{};
  std::uint32_t descriptor_arrays{};
  bool uses_root_constants{};
};

enum class ReflectionError : std::uint8_t {
  None,
  // A static slot is bound that the root layout leaves empty, or whose block
  // the shader description does not list.
  StaticBlockNotDeclared,
  // The draw block is bound and the description declares none.
  DrawBlockNotDeclared,
  // The same slot is reported twice.
  DuplicateBlock,
  TooManyVertexInputs,
  // A semantic name longer than kMaxArtifactString.
  SemanticTooLong,
};
// slot names the failing static slot for StaticBlockNotDeclared and
// DuplicateBlock. On success `reflection` views the registry's static block
// names and the semantic names inside `backend`, which must outlive it.
struct AssembledReflection {
  shaders::Reflection reflection{};
  ReflectionError error{ReflectionError::None};
  std::uint32_t slot{};
};
// Builds the artifact Reflection: each bound static slot becomes a Static
// block named by the root layout's slot and hashed by the description's
// block of that name; the bound draw block becomes the Draw block of the
// description; root_constant_words is the layout's root_constant_words when
// the root constants are used and 0 otherwise; descriptor_arrays, vertex
// inputs and thread group size are copied. Each buffer block the
// description declares becomes a Buffer block at its index in that list,
// whatever the backend reports, since a buffer read binds nothing a
// backend can reflect; a null entry is skipped here and refused by
// append_generated_include. Blocks are emitted in (kind, slot) order,
// static, then draw, then buffer, the order artifact.hpp writes them in.
[[nodiscard]] AssembledReflection assemble_reflection(const BackendReflection& backend,
                                                      const shaders::ShaderDescription& shader,
                                                      const shaders::RootLayout& layout) noexcept;

// ---- input hash -----------------------------------------------------------------

struct IncludedFile {
  // Relative to the source root that holds it, forward slashes; the
  // relative path, not the root, enters the input hash.
  std::string path{};
  std::string text{};
};
struct HashInputs {
  std::string_view compiler_identity{};
  std::string_view entry_point{};
  std::span<const shaders::Define> defines{};
  std::span<const std::string> arguments{};
  std::string_view generated_include{};
  std::string_view main_source{};
  std::span<const IncludedFile> includes{};
  shaders::LayoutHash root_layout_hash{};
  shaders::ShaderTarget target{shaders::ShaderTarget::Direct3D12};
  shaders::ShaderStage stage{shaders::ShaderStage::Vertex};
};
// BLAKE3-256 over, in artifact.hpp's order: kArtifactFormatVersion (four
// little-endian bytes); compiler_identity; the target byte; the stage byte;
// entry_point; the defines as NAME=VALUE; the arguments; root_layout_hash
// (eight little-endian bytes); generated_include; main_source; each include's
// path then text. Every item is preceded by its length as eight
// little-endian bytes, and each of the three lists (defines, arguments,
// includes) by its element count as eight little-endian bytes, so no two
// different input sets produce the same byte stream: the count prefix keeps
// a define from being read as an argument when both lists are short. The
// list counts are stricter than artifact.hpp's item list, which they extend.
// This is the format's own hash (artifact.hpp, InputHash).
[[nodiscard]] shaders::InputHash compute_input_hash(const HashInputs& inputs);
// Lower-case hexadecimal, 64 characters.
[[nodiscard]] std::string input_hash_hex(const shaders::InputHash& hash);
// The Metal Shader Converter's part of the compiler identity
// (toolchain.hpp): "metal-shaderconverter uuid:" then the 16 bytes of the
// loaded library's LC_UUID as 32 lower-case hexadecimal digits, in byte
// order.
[[nodiscard]] std::string converter_identity(const std::array<std::uint8_t, 16>& uuid);

// The LC_UUID of a 64-bit Mach-O image. `image` holds the mach_header_64 (32
// bytes: magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags,
// reserved, each a little-endian u32, the byte order of every Apple target)
// followed by its load commands, at least through sizeofcmds. Each load
// command starts with u32 cmd and u32 cmdsize; LC_UUID (0x1b) is cmd,
// cmdsize, then the 16 UUID bytes. The walk is done in 64-bit arithmetic, so
// no sum of sizes can wrap. No value for: a span shorter than the header; a
// magic other than MH_MAGIC_64 (0xfeedfacf); sizeofcmds running past the
// span; a command list that ends before ncmds commands; a command whose
// cmdsize is below 8 or runs past sizeofcmds; an LC_UUID whose cmdsize is
// below 24; no LC_UUID among the ncmds commands. The first LC_UUID wins.
[[nodiscard]] std::optional<std::array<std::uint8_t, 16>>
mach_o_uuid(std::span<const std::byte> image) noexcept;

// ---- generated include -------------------------------------------------------

enum class IncludeError : std::uint8_t {
  None,
  // A null entry in the description's static blocks.
  NullBlock,
  // A static block of the description has no slot in the root layout
  // (compiler_options.hpp, build-time algorithm step 1).
  StaticBlockWithoutSlot,
  // The root layout's slot for a static block records a different layout
  // hash than the block's own: the engine root layout and the registry
  // disagree, and an artifact built now would bind the wrong layout.
  StaticSlotHashMismatch,
  // A block is reachable from two top-level blocks (as a nested block or as
  // both): append_hlsl_struct emits nested blocks once per call, so the
  // include would define the struct twice.
  SharedBlock,
  // append_hlsl_struct failed; hlsl names how.
  Hlsl,
};
// block names the failing block (or member, for Hlsl); empty on None.
struct IncludeResult {
  std::string_view block{};
  IncludeError error{IncludeError::None};
  shaders::HlslError hlsl{shaders::HlslError::None};
};
// Appends exactly: append_hlsl_struct of the draw block (when present), then
// of each static block in description order, then of each buffer block in
// description order; then append_hlsl_block_binding of the draw block, then
// of each static block in description order with its root layout slot;
// then append_hlsl_buffer_loader of each buffer block in description
// order. NullBlock also names a null buffer block entry, and Hlsl a loader
// that failed. On error `out` holds a partial text the caller discards.
[[nodiscard]] IncludeResult append_generated_include(const shaders::ShaderDescription& shader,
                                                     const shaders::RootLayout& layout,
                                                     std::string& out);
// "bw/generated/<shader>.hlsli": the name the shader includes and the
// include handler serves from memory.
[[nodiscard]] std::string generated_include_path(std::string_view shader_name);

// ---- include paths, depfile, names -------------------------------------------------

// The path DXC asks the include handler for, made relative to the shader
// root: backslashes become slashes, "." and empty components are dropped,
// ".." removes the previous component. Refused (no value): an absolute
// path; a path that climbs above the root; a component containing ':', which
// is a drive letter or URL scheme on the hosts DXC runs on and so names
// something outside the shader root; an empty result.
[[nodiscard]] std::optional<std::string> normalize_include_path(std::string_view requested);

// shader_root as given, joined to a relative path with one '/'.
[[nodiscard]] std::string join_path(std::string_view root, std::string_view relative);

// Makefile dependency text for one artifact:
//   "<target>: \\\n  <root>/<main_source> \\\n  <root>/<include> ... \n"
// With several source roots the caller passes an empty shader_root and
// paths already joined to the root that holds each; with one root the text
// is the same either way.
// one dependency per line, main source first, includes in the order given,
// every line but the last ending in " \\". Paths are written as given
// (shader_root joined to each relative path); a space, '#' and '$' are
// escaped as "\\ ", "\\#" and "$$". The generated include is never listed:
// it is not a file.
void append_depfile(std::string_view target, std::string_view shader_root,
                    std::string_view main_source, std::span<const std::string> includes,
                    std::string& out);

// "<target directory>/<artifact file name>" (artifact.hpp, names).
[[nodiscard]] std::string artifact_relative_path(shaders::ShaderTarget target,
                                                 std::string_view shader_name,
                                                 shaders::PermutationId permutation,
                                                 shaders::ShaderStage stage);
// The symbol file: input_hash_hex plus ".pdb" for the DXIL targets (DXC's
// PDB) or ".spv" for Vulkan (the module as compiled, before debug strip).
[[nodiscard]] std::string symbol_file_name(const shaders::InputHash& hash,
                                           shaders::ShaderTarget target);

} // namespace bw::shader_compiler
#endif
