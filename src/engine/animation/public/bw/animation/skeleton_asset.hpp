#ifndef BW_ANIMATION_SKELETON_ASSET_HPP
#define BW_ANIMATION_SKELETON_ASSET_HPP

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

// Responsibility: the skeleton payload of a cooked asset and its reader
// (docs/architecture/content.md, "Asset kinds": bone hierarchy; the mesh
// names the skeleton id it binds to). A skeleton is the content's bone
// list: names, ids, parent indices, two transform blocks per bone as the
// content stores them, and a field saying which block is the bind pose.
// The animation slice adds clips and bone maps' use; this header is what
// a mesh binds to and what the converter writes.
//
// Layout, little-endian, read by copy: SkeletonPayloadHeader at 0; bone
// records at bones_offset; the string table at strings_offset; both
// offsets multiples of 16 from the payload start.
//
// Contract (decode refuses a payload that breaks a rule below and names a
// broken rule; with several broken, which one is named is not promised):
//   1. The header fits; sections are 16-aligned and within the payload.
//   2. bone_count is at least 1 and at most kMaxBones.
//   3. Bone k's parent is -1 for exactly one bone, and otherwise a value
//      below k (parents precede children; every shipped skeleton holds
//      this). A bone's index is its position in the table; no index is
//      stored.
//   4. Every name is a non-empty slice of the string table of at most 31
//      bytes.
//   5. bind_block is an enumerator of BindBlock. Unknown means the
//      converter had not established which block is the bind pose; a
//      consumer that needs a bind pose fails by name on Unknown rather
//      than guessing. The converter fills A or B once the fact in the
//      plan's list is established.
//   Rotations are quaternions x y z w, unit length as stored (max
//   deviation 3e-8 in the content); block A reads as parent-relative and
//   block B as model-space in the content, but neither reading is
//   promised here beyond the bytes being as stored.
//   The field at +12 (0..3) and the field at +20 (-1 in 7,177 of 7,206
//   bones) are kept as stored under their offsets' names; their meaning
//   is not established.
//
// Core guidelines read: SL.con.3, C.183, Con.5 (as mesh_asset.hpp), I.1
// (the bind block is explicit data, never a default guessed at load).
// Performance guidelines read: CACHE.5 (floats grouped by block as the
// consumer reads them), CDSA.32.
namespace bw::animation {

static_assert(std::endian::native == std::endian::little);

inline constexpr std::uint32_t kMaxBones = 1024;
inline constexpr std::uint32_t kMaxBoneNameBytes = 31;
inline constexpr std::int32_t kNoParent = -1;

// A u32 in the file.
enum class BindBlock : std::uint8_t {
  Unknown = 0,
  A = 1,
  B = 2,
};

struct SkeletonPayloadHeader {
  std::uint32_t bone_count{};
  std::uint32_t bind_block{};
  std::uint32_t string_bytes{};
  std::uint32_t reserved0{};
  std::uint32_t bones_offset{};
  std::uint32_t strings_offset{};
  std::uint32_t reserved1{};
  std::uint32_t reserved2{};
};
static_assert(sizeof(SkeletonPayloadHeader) == 32);
static_assert(std::is_trivially_copyable_v<SkeletonPayloadHeader>);

struct BoneTransformRecord {
  std::array<float, 4> rotation{};
  std::array<float, 3> translation{};
  std::array<float, 3> scale{};
};
static_assert(sizeof(BoneTransformRecord) == 40);

struct BoneRecord {
  std::uint32_t name_offset{};
  std::uint32_t name_length{};
  std::uint32_t id{};
  std::int32_t parent{};
  // The content's fields at entry offsets +12 and +20, as stored.
  std::uint32_t field_12{};
  std::int32_t field_20{};
  BoneTransformRecord a{};
  BoneTransformRecord b{};
};
static_assert(sizeof(BoneRecord) == 104);
static_assert(std::is_trivially_copyable_v<BoneRecord>);

enum class SkeletonAssetError : std::uint8_t {
  None,
  HeaderTruncated,
  SectionMisaligned,
  SectionOutOfBounds,
  BoneCountInvalid,
  // Not exactly one root.
  RootCountInvalid,
  // A parent at or above the bone's own index.
  ParentOrderInvalid,
  NameInvalid,
  BindBlockUnknown,
};

struct SkeletonAssetFailure {
  SkeletonAssetError error{SkeletonAssetError::None};
  std::uint32_t index{};
};

class SkeletonAssetView {
public:
  SkeletonAssetView() = default;
  [[nodiscard]] const SkeletonPayloadHeader& header() const noexcept { return header_; }
  [[nodiscard]] std::uint32_t bone_count() const noexcept { return header_.bone_count; }
  [[nodiscard]] BindBlock bind_block() const noexcept {
    return static_cast<BindBlock>(header_.bind_block);
  }
  // Precondition: index below bone_count.
  [[nodiscard]] BoneRecord bone(std::uint32_t index) const noexcept;
  [[nodiscard]] std::string_view name(std::uint32_t index) const noexcept;

private:
  friend SkeletonAssetFailure decode_skeleton_asset(std::span<const std::byte>,
                                                    SkeletonAssetView&) noexcept;
  std::span<const std::byte> payload_{};
  SkeletonPayloadHeader header_{};
};

[[nodiscard]] SkeletonAssetFailure decode_skeleton_asset(std::span<const std::byte> payload,
                                                         SkeletonAssetView& view) noexcept;

} // namespace bw::animation

#endif
