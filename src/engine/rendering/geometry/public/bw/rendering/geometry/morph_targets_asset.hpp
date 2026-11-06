#ifndef BW_RENDERING_GEOMETRY_MORPH_TARGETS_ASSET_HPP
#define BW_RENDERING_GEOMETRY_MORPH_TARGETS_ASSET_HPP

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

// Responsibility: the morph target payload of a cooked asset and its reader
// (docs/architecture/content.md, "Identity": `model.pw0.mesh.shapes.<n>`
// is its own asset, split out of the mesh container). A set of named
// position deltas over one mesh group's vertices, as the content stores
// them: four IEEE half-floats per vertex per target, x y z and a w of 1.0.
// The animation system's shape clips weight the targets by name; the
// renderer that applies them reads the deltas as stored. No repacking in
// this slice.
//
// Layout, little-endian, read by copy: MorphTargetsPayloadHeader at 0;
// target records at targets_offset; the delta block at deltas_offset,
// target-major then vertex-minor, 8 bytes per vertex; the string table at
// strings_offset. Offsets are multiples of 16 from the payload start.
//
// Contract (decode refuses a payload that breaks a rule below and names a
// broken rule; with several broken, which one is named is not promised):
//   1. The header fits; sections are 16-aligned and within the payload.
//   2. vertex_count and target_count are at least 1; the delta block,
//      target_count * vertex_count * 8 bytes from deltas_offset, ends at or
//      before strings_offset (DeltaSizeMismatch otherwise). No size is
//      stored for it.
//   3. Every target name is a non-empty slice of the string table; names
//      are unique within the asset.
//   4. group is the mesh group this set belongs to; the mesh group's
//      vertex_count equals this vertex_count. The mesh is not available
//      here, so the consumer that pairs them checks that.
//   deltas(target) is the target's bytes; the half values are not decoded
//   here.
//
// Core guidelines read: SL.con.3, C.183, Con.5 (as mesh_asset.hpp).
// Performance guidelines read: CDSA.32 (deltas stay in the authored
// half layout the consumer reads), GDSA.18 (the data is already a
// block-scaled code; decoding belongs to the consumer).
namespace bw::geometry {

static_assert(std::endian::native == std::endian::little);

inline constexpr std::uint32_t kMorphDeltaBytes = 8;

struct MorphTargetsPayloadHeader {
  std::uint32_t group{};
  std::uint32_t vertex_count{};
  std::uint32_t target_count{};
  std::uint32_t string_bytes{};
  std::uint32_t targets_offset{};
  std::uint32_t deltas_offset{};
  std::uint32_t strings_offset{};
  std::uint32_t reserved{};
};
static_assert(sizeof(MorphTargetsPayloadHeader) == 32);
static_assert(std::is_trivially_copyable_v<MorphTargetsPayloadHeader>);

struct MorphTargetRecord {
  std::uint32_t name_offset{};
  std::uint32_t name_length{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
};
static_assert(sizeof(MorphTargetRecord) == 16);

enum class MorphTargetsError : std::uint8_t {
  None,
  HeaderTruncated,
  SectionMisaligned,
  SectionOutOfBounds,
  CountZero,
  DeltaSizeMismatch,
  NameInvalid,
  NameDuplicate,
};

struct MorphTargetsFailure {
  MorphTargetsError error{MorphTargetsError::None};
  std::uint32_t index{};
};

class MorphTargetsView {
public:
  MorphTargetsView() = default;
  [[nodiscard]] const MorphTargetsPayloadHeader& header() const noexcept { return header_; }
  [[nodiscard]] std::uint32_t target_count() const noexcept { return header_.target_count; }
  // Preconditions: target below target_count.
  [[nodiscard]] std::string_view name(std::uint32_t target) const noexcept;
  [[nodiscard]] std::span<const std::byte> deltas(std::uint32_t target) const noexcept;

private:
  friend MorphTargetsFailure decode_morph_targets(std::span<const std::byte>,
                                                  MorphTargetsView&) noexcept;
  std::span<const std::byte> payload_{};
  MorphTargetsPayloadHeader header_{};
};

[[nodiscard]] MorphTargetsFailure decode_morph_targets(std::span<const std::byte> payload,
                                                       MorphTargetsView& view) noexcept;

} // namespace bw::geometry

#endif
