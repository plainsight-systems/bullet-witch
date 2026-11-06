#include "bw/animation/skeleton_asset.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

// Responsibility: the out-of-line part of bw/animation/skeleton_asset.hpp:
// the payload decoder and the view's record reads.
//
// Contract (the header's): the first failure in SkeletonAssetError order
// is named, each check a pass over every bone before the next check runs,
// so a payload that breaks two rules reports the one earlier in the
// enumeration. Records are copied out of the bytes; nothing is read before
// the bytes it lives in are known to be within the payload.
//
// Readings of the header that the checks encode:
//   A parent other than kNoParent that is negative is not "a value below
//   k" for any bone index, so it is ParentOrderInvalid like a parent at or
//   above the bone's own index.
//   bind_block is checked as an enumerator of BindBlock: 0 (Unknown), 1
//   and 2 decode, and the view reads Unknown back for the consumer that
//   needs a bind pose to refuse; any other value is BindBlockUnknown.
//   The record carries no stored own-index field; bone k is the record at
//   position k, so "bone k's own index equals k" holds by construction.
namespace bw::animation {

namespace {

constexpr std::uint64_t kAlignment = 16;

template <class Record>
Record read_record(std::span<const std::byte> payload, std::uint64_t offset) noexcept {
  Record record{};
  std::memcpy(&record, payload.data() + offset, sizeof(Record));
  return record;
}

std::uint64_t bone_offset(const SkeletonPayloadHeader& header, std::uint32_t index) noexcept {
  return header.bones_offset + (static_cast<std::uint64_t>(index) * sizeof(BoneRecord));
}

bool within(std::uint64_t offset, std::uint64_t size, std::uint64_t limit) noexcept {
  return offset <= limit && size <= limit - offset;
}

} // namespace

BoneRecord SkeletonAssetView::bone(std::uint32_t index) const noexcept {
  return read_record<BoneRecord>(payload_, bone_offset(header_, index));
}

std::string_view SkeletonAssetView::name(std::uint32_t index) const noexcept {
  const BoneRecord record = bone(index);
  const std::span<const std::byte> bytes = payload_.subspan(
      static_cast<std::size_t>(header_.strings_offset) + record.name_offset, record.name_length);
  // Viewing bytes as characters is the one cast the language allows for
  // this (char may alias any object).
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

SkeletonAssetFailure decode_skeleton_asset(std::span<const std::byte> payload,
                                           SkeletonAssetView& view) noexcept {
  using E = SkeletonAssetError;
  const std::uint64_t size = payload.size();
  if (size < sizeof(SkeletonPayloadHeader)) {
    return {E::HeaderTruncated, 0};
  }
  const auto header = read_record<SkeletonPayloadHeader>(payload, 0);
  if (header.bones_offset % kAlignment != 0U || header.strings_offset % kAlignment != 0U) {
    return {E::SectionMisaligned, 0};
  }
  const std::uint64_t bones_bytes =
      static_cast<std::uint64_t>(header.bone_count) * sizeof(BoneRecord);
  if (!within(header.bones_offset, bones_bytes, size) ||
      !within(header.strings_offset, header.string_bytes, size)) {
    return {E::SectionOutOfBounds, 0};
  }
  if (header.bone_count == 0U || header.bone_count > kMaxBones) {
    return {E::BoneCountInvalid, 0};
  }
  std::uint32_t roots = 0;
  for (std::uint32_t k = 0; k < header.bone_count; ++k) {
    if (read_record<BoneRecord>(payload, bone_offset(header, k)).parent == kNoParent) {
      ++roots;
    }
  }
  if (roots != 1U) {
    return {E::RootCountInvalid, 0};
  }
  for (std::uint32_t k = 0; k < header.bone_count; ++k) {
    const std::int32_t parent = read_record<BoneRecord>(payload, bone_offset(header, k)).parent;
    if (parent != kNoParent && (parent < 0 || static_cast<std::uint32_t>(parent) >= k)) {
      return {E::ParentOrderInvalid, k};
    }
  }
  for (std::uint32_t k = 0; k < header.bone_count; ++k) {
    const auto record = read_record<BoneRecord>(payload, bone_offset(header, k));
    if (record.name_length == 0U || record.name_length > kMaxBoneNameBytes ||
        !within(record.name_offset, record.name_length, header.string_bytes)) {
      return {E::NameInvalid, k};
    }
  }
  if (header.bind_block > static_cast<std::uint32_t>(BindBlock::B)) {
    return {E::BindBlockUnknown, 0};
  }
  view.payload_ = payload;
  view.header_ = header;
  return {E::None, 0};
}

} // namespace bw::animation
