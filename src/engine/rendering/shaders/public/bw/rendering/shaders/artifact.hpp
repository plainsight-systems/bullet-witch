#ifndef BW_RENDERING_SHADERS_ARTIFACT_HPP
#define BW_RENDERING_SHADERS_ARTIFACT_HPP
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

// Responsibility: the on-disk shader artifact (docs/architecture/shaders.md,
// "Artifact", invariants 7, 8 and 9): one self-describing file per shader,
// permutation and target, holding a header, backend-neutral reflection, the
// code block and a keyed trailer. The writer is used by shader_compiler; the
// loader by the RHI. Nothing here touches a file: both work on bytes.
//
// Contract. write_artifact takes the header fields, the reflection, the code
// bytes and the trailer entries and produces the complete byte image, or a
// named ArtifactWriteError; equal inputs give identical bytes: blocks and
// vertex inputs are written in canonical order whatever order the Reflection
// holds them, trailer entries in the order given. load_artifact takes the
// bytes and the running declarations (LoadExpectations) and returns either
// views into the bytes or a named LoadError; it never reads outside the
// span. Its checks, in this order, each with the code it returns:
//   1. at least the fixed header's 76 bytes: Truncated;
//   2. magic: BadMagic;
//   3. format version equal to kArtifactFormatVersion: UnsupportedVersion;
//   4. total_size equal to the span's size: SizeMismatch;
//   5. target and stage values known: UnknownTarget, UnknownStage;
//   6. header_size and reflection_size multiples of 4, code_size non-zero,
//      header_size at least the fixed header, every reserved field zero:
//      MalformedHeader;
//   7. the four sections summing exactly to total_size, each within the
//      span: Truncated when a section runs past the end, MalformedHeader
//      when they fall short;
//   8. the two header strings within header_size: MalformedHeader;
//   9. reflection well formed: counts within their maxima, every string
//      within the section, kinds and slots in range, descriptor_arrays bits
//      only below kDescriptorArrayCount, thread group zero unless the stage
//      is compute, no two blocks with the same (kind, slot):
//      MalformedReflection;
//  10. trailer closed by its length, every entry within it: TrailerCorrupt;
//  11. permutation id below permutation_count when that is non-zero:
//      PermutationOutOfRange;
//  12. root layout hash equal to root_layout's hash; a null root_layout is a
//      mismatch with expected 0: RootLayoutMismatch;
//  13. every reflected block present in running_blocks by name: UnknownBlock;
//  14. its layout hash equal: LayoutHashMismatch;
//  15. for a static block, its slot equal to find_static_slot in the root
//      layout; expected is kMaxStaticBlocks when the layout has no slot for
//      it: SlotMismatch.
// Each check is a test; the order is observable only where one image fails
// two checks. Ownership: the Artifact borrows the bytes; the caller keeps
// them alive while it uses the views. No allocation on the load path; the
// writer allocates its output vector. No synchronization: pure functions.
//
// Byte format, all integers little-endian, version 1:
//   Header   u32 magic 'BWSH' (0x48535742); u32 format_version; u32 total_size;
//            u32 header_size; u8 target; u8 stage; u16 zero; u32 permutation;
//            u8[32] input_hash; u64 root_layout_hash; u32 reflection_size;
//            u32 code_size; u32 trailer_size; u32 name_length, name bytes;
//            u32 entry_length, entry bytes; zero bytes to a multiple of 4.
//            header_size is the length of all of that.
//   Reflection, reflection_size bytes, a multiple of 4:
//            u32 descriptor_arrays (bit i set when the shader declares and
//            uses DescriptorArray i; higher bits zero); u32
//            root_constant_words; u32[3] thread group size (zero unless
//            compute); u32 block_count; per block u8 kind (0 static, 1
//            draw), u8 slot, u16 zero, u64 layout_hash, u32 name_length,
//            name bytes, zero to a multiple of 4; u32 vertex_input_count;
//            per input u32 location, u32 semantic_index, u32 name_length,
//            semantic bytes, zero to a multiple of 4. Blocks are in (kind,
//            slot) order, inputs in (location, semantic_index, name) order.
//   Code     code_size bytes: a DXIL container, SPIR-V words or a Metal
//            library. Never compressed.
//   Trailer  entries of u8 key, u32 size, size bytes; then u32 total, the
//            byte count of the entries plus 4. trailer_size equals total.
//   total_size == header_size + reflection_size + code_size + trailer_size.
//
// Design and cost: a load is one pass over the header, reflection and
// trailer, O(bytes of those sections), with the code block untouched; the
// reflection is decoded into fixed-capacity arrays sized by the root layout,
// so the Artifact has no heap state. The reflection block is identical across
// the three targets for the same shader and permutation; that equality is an
// integration test.
//
// Core guidelines read: E.27 (a result type with a named error code; no
// exceptions), SL.io.2 and SL.con.3 (every length is checked before use;
// views are cut from the span, never from raw pointers), I.1 (expectations
// are a parameter, not a registry), C.32 and SL.str.2 (views are borrowed and
// said to be), F.6 (the loader is noexcept). Performance guidelines read:
// CDSA.32 (the artifact is the consumer's layout, built once at build time,
// validated and versioned), MEM.9 (the RHI load path allocates nothing),
// COPY.4 (Artifact and the result type own nothing and declare no special
// members).
namespace bw::shaders {

inline constexpr std::uint32_t kArtifactMagic = 0x48535742U;
inline constexpr std::uint32_t kArtifactFormatVersion = 1;
inline constexpr std::size_t kInputHashBytes = 32;
inline constexpr std::size_t kMaxReflectedBlocks = kMaxStaticBlocks + 1;
inline constexpr std::size_t kMaxVertexInputs = 16;
// Bound on every string in the format (shader name, entry point, block and
// semantic names).
inline constexpr std::size_t kMaxArtifactString = 255;

// BLAKE3-256 of the artifact's inputs, computed by the tool over, in order,
// each item length-prefixed: format version; compiler identity (DXC's
// version and commit, and for Metal the converter library's own identity,
// its Mach-O UUID, since it reports no version at run time); target byte;
// stage byte;
// entry point; each define as NAME=VALUE in domain order; each compiler
// argument in order; root layout hash; the generated include text; the
// main source text; each included file's path relative to the shader root
// and its text, in first-inclusion order. Two artifacts with equal input
// hashes are byte-identical on every host; a difference fails the build
// (invariant 3, an integration test).
using InputHash = std::array<std::uint8_t, kInputHashBytes>;

struct ArtifactHeader {
  std::string_view shader_name{};
  std::string_view entry_point{};
  InputHash input_hash{};
  LayoutHash root_layout_hash{};
  PermutationId permutation{};
  ShaderTarget target{ShaderTarget::Direct3D12};
  ShaderStage stage{ShaderStage::Vertex};
};

// Backend-neutral reflection: the set of bound blocks with a layout hash each,
// and usage flags. Every resource is reached by an index from a block into a
// typed descriptor array (invariant 5), so the only resource facts a backend
// needs are which arrays the shader uses.
enum class BlockSlotKind : std::uint8_t {
  Static = 0,
  Draw = 1,
};
struct ReflectedBlock {
  std::string_view name{};
  LayoutHash layout_hash{};
  std::uint32_t slot{};
  BlockSlotKind kind{BlockSlotKind::Static};
};
struct VertexInput {
  std::string_view semantic_name{};
  std::uint32_t semantic_index{};
  std::uint32_t location{};
};
struct Reflection {
  std::array<ReflectedBlock, kMaxReflectedBlocks> blocks{};
  std::array<VertexInput, kMaxVertexInputs> vertex_inputs{};
  std::array<std::uint32_t, 3> thread_group_size{};
  std::uint32_t block_count{};
  std::uint32_t vertex_input_count{};
  std::uint32_t root_constant_words{};
  // Bit (1 << DescriptorArray) per array declared and used.
  std::uint32_t descriptor_arrays{};
  [[nodiscard]] std::span<const ReflectedBlock> bound_blocks() const noexcept {
    return {blocks.data(), block_count};
  }
  [[nodiscard]] std::span<const VertexInput> inputs() const noexcept {
    return {vertex_inputs.data(), vertex_input_count};
  }
};

// Trailer keys. Unknown keys are kept and ignored by the loader, so a later
// tool may add one within this format version.
// Keys are single characters. The header carries the shader name and entry
// point, symbols live in a separate file and source never ships, so none of
// those has a key.
enum class TrailerKey : std::uint8_t {
  Features = 'x',
  CompilerIdentity = 'C',
};
// Features, stored as a u32 bit set under TrailerKey::Features, from the DXIL
// feature flags.
enum class ShaderFeature : std::uint8_t {
  WaveOperations = 1U << 0U,
  SixteenBitTypes = 1U << 1U,
};
struct TrailerEntry {
  std::span<const std::byte> bytes{};
  TrailerKey key{TrailerKey::Features};
};

// Named write failures, checked in this order.
enum class ArtifactWriteError : std::uint8_t {
  None,
  // target or stage is not an enumerator.
  InvalidTargetOrStage,
  // A header, block or semantic string longer than kMaxArtifactString.
  StringTooLong,
  TooManyBlocks,
  TooManyVertexInputs,
  // A static slot at or above kMaxStaticBlocks, a draw slot other than 0, or
  // a kind that is not an enumerator.
  SlotOutOfRange,
  // Two blocks with the same (kind, slot).
  DuplicateSlot,
  // A non-zero thread group size for a stage other than compute.
  ThreadGroupOnNonCompute,
  // descriptor_arrays has a bit at or above kDescriptorArrayCount.
  UnknownDescriptorArray,
  EmptyCode,
  // One trailer entry's bytes do not fit its u32 size.
  TrailerEntryTooLarge,
  // The code block or the whole image does not fit the u32 size fields.
  ImageTooLarge,
};
// Writes the complete artifact into out (replacing its contents). Blocks are
// written in (kind, slot) order and inputs in (location, semantic_index,
// name) order whatever order the Reflection holds them; trailer entries in
// the order given. Equal inputs give identical bytes.
// An entry is u8 key, u32 size, bytes; the trailer is closed by a u32 holding
// its own length, written last. Artifacts are uncompressed and symbols are a
// separate file named by the input hash. No backend writes a prefix ahead of
// the code: one header and one reflection block serve all three targets
// (invariant 8).
[[nodiscard]] ArtifactWriteError write_artifact(const ArtifactHeader& header,
                                                const Reflection& reflection,
                                                std::span<const std::byte> code,
                                                std::span<const TrailerEntry> trailer,
                                                std::vector<std::byte>& out);

// Named load failures; the contract above gives the check each belongs to.
// Truncated arises at checks 1 and 7, MalformedHeader at 6, 7 and 8.
enum class ArtifactError : std::uint8_t {
  None,
  Truncated,
  BadMagic,
  UnsupportedVersion,
  SizeMismatch,
  UnknownTarget,
  UnknownStage,
  MalformedHeader,
  MalformedReflection,
  TrailerCorrupt,
  PermutationOutOfRange,
  RootLayoutMismatch,
  UnknownBlock,
  LayoutHashMismatch,
  SlotMismatch,
};
// block_name is set for UnknownBlock, LayoutHashMismatch and SlotMismatch;
// expected and actual carry the two hashes, slots, sizes or versions the
// check compared (expected is kMaxStaticBlocks for a SlotMismatch on a block
// the root layout does not place, and 0 for a RootLayoutMismatch with a null
// layout); byte_offset is where the loader stopped.
struct LoadError {
  std::string_view block_name{};
  std::uint64_t expected{};
  std::uint64_t actual{};
  std::uint32_t byte_offset{};
  ArtifactError code{ArtifactError::None};
};

// The running declarations the artifact is checked against. permutation_count
// 0 skips the range check (the caller has no description).
struct LoadExpectations {
  const RootLayout* root_layout{};
  std::span<const BlockLayout* const> running_blocks{};
  std::uint32_t permutation_count{};
};

// Views into the loaded bytes; valid while the bytes are. trailer spans the
// whole trailer section, closing length included, so trailer.size() equals
// the header's trailer_size and find_trailer_entry takes it as is.
struct Artifact {
  ArtifactHeader header{};
  Reflection reflection{};
  std::span<const std::byte> code{};
  std::span<const std::byte> trailer{};
};

class ArtifactLoadResult {
public:
  [[nodiscard]] bool ok() const noexcept { return error_.code == ArtifactError::None; }
  // Precondition: ok().
  [[nodiscard]] const Artifact& artifact() const noexcept { return artifact_; }
  // Precondition: !ok().
  [[nodiscard]] const LoadError& error() const noexcept { return error_; }
  [[nodiscard]] static ArtifactLoadResult success(const Artifact& artifact) noexcept {
    ArtifactLoadResult result{};
    result.artifact_ = artifact;
    return result;
  }
  [[nodiscard]] static ArtifactLoadResult failure(const LoadError& error) noexcept {
    ArtifactLoadResult result{};
    result.error_ = error;
    return result;
  }

private:
  Artifact artifact_{};
  LoadError error_{};
};

// The trailer's closing u32 is its own length; records are walked as key,
// size, value and the walk excludes the closing length.
[[nodiscard]] ArtifactLoadResult load_artifact(std::span<const std::byte> bytes,
                                               const LoadExpectations& expectations) noexcept;

// The bytes of the first trailer entry with that key, or no value. trailer
// is Artifact::trailer (closing length included); a malformed trailer yields
// no value.
[[nodiscard]] std::optional<std::span<const std::byte>>
find_trailer_entry(std::span<const std::byte> trailer, TrailerKey key) noexcept;

// What the shader uses of the root layout, from its reflection alone: the
// static slot bits, the draw block hash, root constant words, the descriptor
// arrays. The same reflection gives the same value whatever the target (a
// unit test).
[[nodiscard]] BindingUse derive_binding_use(const Reflection& reflection) noexcept;

// Names. An artifact is "<shader>.<permutation>.<suffix>.bwsh" under
// "<build>/shaders/<target directory>/", suffix vs, ps or cs and target
// directory d3d12, vulkan or metal. Shaders are identified by this name and
// the content hash in the header, never by position in a table (invariant 7).
[[nodiscard]] std::string_view stage_suffix(ShaderStage stage) noexcept;
[[nodiscard]] std::string_view target_directory(ShaderTarget target) noexcept;
[[nodiscard]] std::string artifact_file_name(std::string_view shader_name,
                                             PermutationId permutation, ShaderStage stage);

} // namespace bw::shaders
#endif
