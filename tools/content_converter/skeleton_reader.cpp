#include "bw/content_converter/skeleton_reader.hpp"

#include "bw/animation/skeleton_asset.hpp"
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"

#include "bytes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Responsibility: the out-of-line part of
// bw/content_converter/skeleton_reader.hpp: the two skeleton records read
// into skeleton data.
//
// Contract (the header's): read_skeleton names the first failure in
// SkeletonReadError order, each check a pass over every bone before the next
// check runs. The bind block stays Unknown; the caller sets it.
//
// Readings of the header that the checks encode:
//   The records are found by short name ("motion.bil", "motion._bl") and
//   must be USER records; a record of either name with another tag counts
//   as absent.
//   BadMagic is either record's four-byte tag, or a record too short to
//   hold one; SizeMismatch is a record shorter than its 8-byte head or not
//   exactly 8 plus its count times its entry size, each against its own
//   count; CountMismatch follows.
//   IndexMismatch covers both index fields (motion.bil +36, motion._bl +8).
//   RootCountInvalid counts the -1 parents; ParentInvalid is any other
//   parent that is negative or not below the bone's index, and comes first
//   in the enumeration, so a second root is reported as RootCountInvalid
//   only when every parent is otherwise in order.
//   NameInvalid is a name field with no NUL in its 32 bytes or an empty
//   name.
namespace bw::content_converter {

namespace {

constexpr std::array<char, 4> kBilMagic{'M', 'B', 'D', 'B'};
constexpr std::array<char, 4> kBlMagic{'_', 'B', 'L', 'B'};
constexpr std::size_t kRecordHeadBytes = 8;
constexpr std::size_t kNameBytes = 32;

const CprRecord* find_user(const CprContainer& container, std::string_view name) noexcept {
  const CprRecord* record = container.find_record(name);
  return record != nullptr && record->tag == RecordTag::User ? record : nullptr;
}

animation::BoneTransformRecord read_transform(std::span<const std::byte> data,
                                              std::size_t at) noexcept {
  animation::BoneTransformRecord t{};
  for (std::size_t k = 0; k < 4; ++k) {
    t.rotation[k] = bytes::f32(data, at + (4U * k));
  }
  for (std::size_t k = 0; k < 3; ++k) {
    t.translation[k] = bytes::f32(data, at + 16U + (4U * k));
    t.scale[k] = bytes::f32(data, at + 28U + (4U * k));
  }
  return t;
}

} // namespace

SkeletonReadFailure read_skeleton(const CprContainer& container, SkeletonAssetData& out) {
  using E = SkeletonReadError;
  out = SkeletonAssetData{};
  const CprRecord* bil = find_user(container, "motion.bil");
  const CprRecord* bl = find_user(container, "motion._bl");
  if (bil == nullptr && bl == nullptr) {
    return {E::NoSkeleton, 0};
  }
  if (bil == nullptr || bl == nullptr) {
    return {E::RecordPairIncomplete, 0};
  }
  const std::span<const std::byte> names = bil->payload;
  const std::span<const std::byte> tree = bl->payload;
  if (names.size() < 4U || tree.size() < 4U || bytes::tag(names, 0) != kBilMagic ||
      bytes::tag(tree, 0) != kBlMagic) {
    return {E::BadMagic, 0};
  }
  if (names.size() < kRecordHeadBytes || tree.size() < kRecordHeadBytes) {
    return {E::SizeMismatch, 0};
  }
  const std::uint64_t count = bytes::u32(names, 4);
  const std::uint64_t tree_count = bytes::u32(tree, 4);
  if (names.size() != kRecordHeadBytes + (count * kBilEntryBytes) ||
      tree.size() != kRecordHeadBytes + (tree_count * kBlEntryBytes)) {
    return {E::SizeMismatch, 0};
  }
  if (count != tree_count) {
    return {E::CountMismatch, 0};
  }
  const auto bil_at = [](std::size_t k) { return kRecordHeadBytes + (k * kBilEntryBytes); };
  const auto bl_at = [](std::size_t k) { return kRecordHeadBytes + (k * kBlEntryBytes); };
  for (std::size_t k = 0; k < count; ++k) {
    if (bytes::u32(names, bil_at(k) + 36U) != k || bytes::u32(tree, bl_at(k) + 8U) != k) {
      return {E::IndexMismatch, static_cast<std::uint32_t>(k)};
    }
  }
  std::uint32_t roots = 0;
  for (std::size_t k = 0; k < count; ++k) {
    const std::int32_t parent = bytes::i32(tree, bl_at(k) + 4U);
    if (parent == animation::kNoParent) {
      ++roots;
    } else if (parent < 0 || static_cast<std::uint64_t>(parent) >= k) {
      return {E::ParentInvalid, static_cast<std::uint32_t>(k)};
    }
  }
  if (roots != 1U) {
    return {E::RootCountInvalid, 0};
  }
  std::vector<BoneData> bones(count);
  for (std::size_t k = 0; k < count; ++k) {
    const bytes::CString name = bytes::c_string(names.subspan(bil_at(k) + 4U, kNameBytes), 0);
    if (!name.found || name.text.empty()) {
      return {E::NameInvalid, static_cast<std::uint32_t>(k)};
    }
    BoneData& bone = bones[k];
    bone.name = std::string(name.text);
    bone.id = bytes::u32(names, bil_at(k));
    bone.parent = bytes::i32(tree, bl_at(k) + 4U);
    bone.field_12 = bytes::u32(tree, bl_at(k) + 12U);
    bone.field_20 = bytes::i32(tree, bl_at(k) + 20U);
    bone.a = read_transform(tree, bl_at(k) + 24U);
    bone.b = read_transform(tree, bl_at(k) + 64U);
  }
  out.bind_block = animation::BindBlock::Unknown;
  out.bones = std::move(bones);
  return {E::None, 0};
}

} // namespace bw::content_converter
