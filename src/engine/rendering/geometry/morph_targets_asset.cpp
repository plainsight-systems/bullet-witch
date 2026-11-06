#include "bw/rendering/geometry/morph_targets_asset.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

// Responsibility: the out-of-line part of
// bw/rendering/geometry/morph_targets_asset.hpp: the payload decoder and the
// view's name and delta reads.
//
// Contract (the header's): the first failure in MorphTargetsError order is
// named, each check a pass over every target before the next check runs.
// Records are copied out of the bytes; nothing is read before the bytes it
// lives in are known to be within the payload. The half values are not
// decoded.
//
// Readings of the header that the checks encode:
//   The payload stores no delta block size: the block's size is derived,
//   target_count * vertex_count * kMorphDeltaBytes. Contract 1 therefore
//   checks the delta block's offset (16-aligned, within the payload), and
//   DeltaSizeMismatch, after CountZero, is a derived block that ends past
//   strings_offset, a block starting past strings_offset included. The
//   string table is within the payload by then, so such a block also
//   cannot run past the payload's end.
//   The index reported with NameInvalid and NameDuplicate is the target's;
//   for a duplicate, the later of the two.
namespace bw::geometry {

namespace {

constexpr std::uint64_t kAlignment = 16;

template <class Record>
Record read_record(std::span<const std::byte> payload, std::uint64_t offset) noexcept {
  Record record{};
  std::memcpy(&record, payload.data() + offset, sizeof(Record));
  return record;
}

bool within(std::uint64_t offset, std::uint64_t size, std::uint64_t limit) noexcept {
  return offset <= limit && size <= limit - offset;
}

MorphTargetRecord read_target(std::span<const std::byte> payload,
                              const MorphTargetsPayloadHeader& header,
                              std::uint32_t index) noexcept {
  return read_record<MorphTargetRecord>(
      payload,
      header.targets_offset + (static_cast<std::uint64_t>(index) * sizeof(MorphTargetRecord)));
}

std::string_view target_name(std::span<const std::byte> payload,
                             const MorphTargetsPayloadHeader& header,
                             const MorphTargetRecord& record) noexcept {
  const std::span<const std::byte> bytes = payload.subspan(
      static_cast<std::size_t>(header.strings_offset) + record.name_offset, record.name_length);
  // Viewing bytes as characters is the one cast the language allows for
  // this (char may alias any object).
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::uint64_t target_delta_bytes(const MorphTargetsPayloadHeader& header) noexcept {
  return static_cast<std::uint64_t>(header.vertex_count) * kMorphDeltaBytes;
}

} // namespace

std::string_view MorphTargetsView::name(std::uint32_t target) const noexcept {
  return target_name(payload_, header_, read_target(payload_, header_, target));
}

std::span<const std::byte> MorphTargetsView::deltas(std::uint32_t target) const noexcept {
  const std::uint64_t bytes = target_delta_bytes(header_);
  return payload_.subspan(header_.deltas_offset + (target * bytes), bytes);
}

MorphTargetsFailure decode_morph_targets(std::span<const std::byte> payload,
                                         MorphTargetsView& view) noexcept {
  using E = MorphTargetsError;
  const std::uint64_t size = payload.size();
  if (size < sizeof(MorphTargetsPayloadHeader)) {
    return {E::HeaderTruncated, 0};
  }
  const auto h = read_record<MorphTargetsPayloadHeader>(payload, 0);
  if (h.targets_offset % kAlignment != 0U || h.deltas_offset % kAlignment != 0U ||
      h.strings_offset % kAlignment != 0U) {
    return {E::SectionMisaligned, 0};
  }
  const std::uint64_t table_bytes =
      static_cast<std::uint64_t>(h.target_count) * sizeof(MorphTargetRecord);
  if (!within(h.targets_offset, table_bytes, size) || h.deltas_offset > size ||
      !within(h.strings_offset, h.string_bytes, size)) {
    return {E::SectionOutOfBounds, 0};
  }
  if (h.vertex_count == 0U || h.target_count == 0U) {
    return {E::CountZero, 0};
  }
  // target_count * per_target can exceed 64 bits; compare by division.
  if (h.deltas_offset > h.strings_offset ||
      h.target_count > (h.strings_offset - h.deltas_offset) / target_delta_bytes(h)) {
    return {E::DeltaSizeMismatch, 0};
  }
  for (std::uint32_t t = 0; t < h.target_count; ++t) {
    const MorphTargetRecord record = read_target(payload, h, t);
    if (record.name_length == 0U ||
        !within(record.name_offset, record.name_length, h.string_bytes)) {
      return {E::NameInvalid, t};
    }
  }
  for (std::uint32_t t = 1; t < h.target_count; ++t) {
    const std::string_view name = target_name(payload, h, read_target(payload, h, t));
    for (std::uint32_t u = 0; u < t; ++u) {
      if (target_name(payload, h, read_target(payload, h, u)) == name) {
        return {E::NameDuplicate, t};
      }
    }
  }
  view.payload_ = payload;
  view.header_ = h;
  return {E::None, 0};
}

} // namespace bw::geometry
