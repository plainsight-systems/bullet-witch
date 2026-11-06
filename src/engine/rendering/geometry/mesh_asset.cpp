#include "bw/rendering/geometry/mesh_asset.hpp"

#include "bw/content/asset_id.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

// Responsibility: the out-of-line part of
// bw/rendering/geometry/mesh_asset.hpp: the payload decoder and the view's
// record reads.
//
// Contract (the header's): the first failure in MeshAssetError order is
// named, each check a pass over every group, draw or bone map before the
// next check runs, so a payload that breaks two rules reports the one
// earlier in the enumeration whichever record breaks it. Records are copied
// out of the bytes; nothing is read before the bytes it lives in are known
// to be within the payload. Index values are not read (contract 6).
//
// Readings of the header that the checks encode:
//   SectionMisaligned and SectionOutOfBounds cover the five sections the
//   payload header names (groups, draws, bone maps, bone entries, strings),
//   reported with index 0. A group's stream or index range that is not
//   16-aligned or runs past the payload is StreamOutOfBounds or
//   IndexOutOfBounds for that group, as contracts 3 and 4 place alignment.
//   AttributeUnknown scans the first min(attribute_count,
//   kVertexAttributeCount) attribute records; a larger count is the layout
//   validator's TooManyAttributes, reported as LayoutInvalid.
//   "stream1 fields are zero when stream_count is 1" has no enumerator of
//   its own; stride1, stream1_offset or stream1_size non-zero on a
//   one-stream group is StreamSizeMismatch for that group.
//   A draw whose index_count is not a multiple of 3 or is below 3 is
//   DrawIndexRangeInvalid, the error for the draw's index range.
//   DrawRangeInvalid names the group whose first_draw breaks the sequence,
//   or the last group when the sequence ends short of or past draw_count.
//   An empty skeleton id is "no skeleton" whatever its offset; a present
//   one whose slice leaves the string table is SkeletonIdInvalid.
namespace bw::geometry {

namespace {

constexpr std::uint64_t kAlignment = kMeshSectionAlignment;
constexpr std::uint64_t kIndexBytes = sizeof(std::uint16_t);

template <class Record>
Record read_record(std::span<const std::byte> payload, std::uint64_t offset) noexcept {
  Record record{};
  std::memcpy(&record, payload.data() + offset, sizeof(Record));
  return record;
}

bool within(std::uint64_t offset, std::uint64_t size, std::uint64_t limit) noexcept {
  return offset <= limit && size <= limit - offset;
}

bool aligned(std::uint64_t offset) noexcept { return offset % kAlignment == 0U; }

std::uint64_t element_offset(std::uint32_t base, std::uint32_t index,
                             std::uint64_t element_bytes) noexcept {
  return base + (static_cast<std::uint64_t>(index) * element_bytes);
}

MeshGroupRecord read_group(std::span<const std::byte> payload, const MeshPayloadHeader& header,
                           std::uint32_t index) noexcept {
  return read_record<MeshGroupRecord>(
      payload, element_offset(header.groups_offset, index, sizeof(MeshGroupRecord)));
}
MeshDrawRecord read_draw(std::span<const std::byte> payload, const MeshPayloadHeader& header,
                         std::uint32_t index) noexcept {
  return read_record<MeshDrawRecord>(
      payload, element_offset(header.draws_offset, index, sizeof(MeshDrawRecord)));
}
MeshBoneMapRecord read_bone_map(std::span<const std::byte> payload, const MeshPayloadHeader& header,
                                std::uint32_t index) noexcept {
  return read_record<MeshBoneMapRecord>(
      payload, element_offset(header.bone_maps_offset, index, sizeof(MeshBoneMapRecord)));
}

std::uint32_t scanned_attributes(const MeshGroupRecord& group) noexcept {
  return group.attribute_count < kVertexAttributeCount ? group.attribute_count
                                                       : kVertexAttributeCount;
}

// Precondition: the scanned attribute and type bytes are enumerators
// (checked by the AttributeUnknown pass before any layout is built).
VertexStreamLayout layout_of(const MeshGroupRecord& group) noexcept {
  VertexStreamLayout layout{};
  const std::uint32_t count = scanned_attributes(group);
  for (std::uint32_t i = 0; i < count; ++i) {
    const MeshAttributeRecord& a = group.attributes[i];
    layout.attributes[i] =
        VertexAttributeDescription{static_cast<VertexAttribute>(a.attribute),
                                   static_cast<VertexElementType>(a.type), a.stream, a.offset};
  }
  layout.streams[0].stride = group.stride0;
  layout.streams[1].stride = group.stride1;
  layout.attribute_count = group.attribute_count;
  layout.stream_count = group.stream_count;
  return layout;
}

struct Section {
  std::uint32_t offset{};
  std::uint64_t bytes{};
};

MeshAssetFailure check_sections(const MeshPayloadHeader& h, std::uint64_t size) noexcept {
  const std::array<Section, 5> sections{
      Section{h.groups_offset, static_cast<std::uint64_t>(h.group_count) * sizeof(MeshGroupRecord)},
      Section{h.draws_offset, static_cast<std::uint64_t>(h.draw_count) * sizeof(MeshDrawRecord)},
      Section{h.bone_maps_offset,
              static_cast<std::uint64_t>(h.bone_map_count) * sizeof(MeshBoneMapRecord)},
      Section{h.bone_entries_offset,
              static_cast<std::uint64_t>(h.bone_entry_count) * sizeof(std::uint32_t)},
      Section{h.strings_offset, h.string_bytes},
  };
  for (const Section& s : sections) {
    if (!aligned(s.offset)) {
      return {MeshAssetError::SectionMisaligned, 0, VertexStreamError::None};
    }
  }
  for (const Section& s : sections) {
    if (!within(s.offset, s.bytes, size)) {
      return {MeshAssetError::SectionOutOfBounds, 0, VertexStreamError::None};
    }
  }
  return {};
}

// Contracts 2 to 4, one pass per error over every group.
MeshAssetFailure check_groups(std::span<const std::byte> payload,
                              const MeshPayloadHeader& h) noexcept {
  using E = MeshAssetError;
  const std::uint64_t size = payload.size();
  for (std::uint32_t g = 0; g < h.group_count; ++g) {
    const MeshGroupRecord group = read_group(payload, h, g);
    for (std::uint32_t i = 0; i < scanned_attributes(group); ++i) {
      if (group.attributes[i].attribute >= kVertexAttributeCount ||
          group.attributes[i].type >= kVertexElementTypeCount) {
        return {E::AttributeUnknown, g, VertexStreamError::None};
      }
    }
  }
  for (std::uint32_t g = 0; g < h.group_count; ++g) {
    const VertexStreamError error =
        validate_vertex_stream_layout(layout_of(read_group(payload, h, g)));
    if (error != VertexStreamError::None) {
      return {E::LayoutInvalid, g, error};
    }
  }
  for (std::uint32_t g = 0; g < h.group_count; ++g) {
    if (read_group(payload, h, g).vertex_count == 0U) {
      return {E::VertexCountZero, g, VertexStreamError::None};
    }
  }
  for (std::uint32_t g = 0; g < h.group_count; ++g) {
    const MeshGroupRecord group = read_group(payload, h, g);
    const std::uint64_t vertices = group.vertex_count;
    const bool stream0_sized = group.stream0_size == vertices * group.stride0;
    const bool stream1_sized =
        group.stream_count == 1U
            ? (group.stride1 == 0U && group.stream1_offset == 0U && group.stream1_size == 0U)
            : group.stream1_size == vertices * group.stride1;
    if (!stream0_sized || !stream1_sized) {
      return {E::StreamSizeMismatch, g, VertexStreamError::None};
    }
  }
  for (std::uint32_t g = 0; g < h.group_count; ++g) {
    const MeshGroupRecord group = read_group(payload, h, g);
    const bool stream0_placed =
        aligned(group.stream0_offset) && within(group.stream0_offset, group.stream0_size, size);
    const bool stream1_placed =
        group.stream_count == 1U ||
        (aligned(group.stream1_offset) && within(group.stream1_offset, group.stream1_size, size));
    if (!stream0_placed || !stream1_placed) {
      return {E::StreamOutOfBounds, g, VertexStreamError::None};
    }
  }
  for (std::uint32_t g = 0; g < h.group_count; ++g) {
    const std::uint32_t count = read_group(payload, h, g).index_count;
    if (count < 3U || count % 3U != 0U) {
      return {E::IndexCountInvalid, g, VertexStreamError::None};
    }
  }
  for (std::uint32_t g = 0; g < h.group_count; ++g) {
    const MeshGroupRecord group = read_group(payload, h, g);
    if (!aligned(group.index_offset) ||
        !within(group.index_offset, group.index_count * kIndexBytes, size)) {
      return {E::IndexOutOfBounds, g, VertexStreamError::None};
    }
  }
  return {};
}

// Contracts 5 and 6, one pass per error; the per-draw passes walk each
// group's draw range, which the first pass has proven consecutive.
MeshAssetFailure check_draws(std::span<const std::byte> payload,
                             const MeshPayloadHeader& h) noexcept {
  using E = MeshAssetError;
  std::uint64_t next = 0;
  for (std::uint32_t g = 0; g < h.group_count; ++g) {
    const MeshGroupRecord group = read_group(payload, h, g);
    if (group.first_draw != next) {
      return {E::DrawRangeInvalid, g, VertexStreamError::None};
    }
    next += group.draw_count;
  }
  if (next != h.draw_count) {
    return {E::DrawRangeInvalid, h.group_count - 1U, VertexStreamError::None};
  }
  for (std::uint32_t g = 0; g < h.group_count; ++g) {
    const MeshGroupRecord group = read_group(payload, h, g);
    for (std::uint32_t d = group.first_draw; d < group.first_draw + group.draw_count; ++d) {
      const MeshDrawRecord draw = read_draw(payload, h, d);
      if (draw.index_count < 3U || draw.index_count % 3U != 0U ||
          static_cast<std::uint64_t>(draw.first_index) + draw.index_count > group.index_count) {
        return {E::DrawIndexRangeInvalid, d, VertexStreamError::None};
      }
    }
  }
  for (std::uint32_t g = 0; g < h.group_count; ++g) {
    const MeshGroupRecord group = read_group(payload, h, g);
    for (std::uint32_t d = group.first_draw; d < group.first_draw + group.draw_count; ++d) {
      const MeshDrawRecord draw = read_draw(payload, h, d);
      if (static_cast<std::uint64_t>(draw.base_vertex) + draw.min_index + draw.vertex_count >
          group.vertex_count) {
        return {E::DrawVertexRangeInvalid, d, VertexStreamError::None};
      }
    }
  }
  for (std::uint32_t d = 0; d < h.draw_count; ++d) {
    const std::uint32_t material = read_draw(payload, h, d).material;
    if (material != kNoMaterial && material >= h.material_count) {
      return {E::MaterialOutOfRange, d, VertexStreamError::None};
    }
  }
  for (std::uint32_t d = 0; d < h.draw_count; ++d) {
    const std::uint32_t map = read_draw(payload, h, d).bone_map;
    if (map != kNoBoneMap && map >= h.bone_map_count) {
      return {E::BoneMapOutOfRange, d, VertexStreamError::None};
    }
  }
  return {};
}

} // namespace

MeshGroupRecord MeshAssetView::group(std::uint32_t index) const noexcept {
  return read_group(payload_, header_, index);
}
MeshDrawRecord MeshAssetView::draw(std::uint32_t index) const noexcept {
  return read_draw(payload_, header_, index);
}
MeshBoneMapRecord MeshAssetView::bone_map(std::uint32_t index) const noexcept {
  return read_bone_map(payload_, header_, index);
}
std::uint32_t MeshAssetView::bone_entry(std::uint32_t index) const noexcept {
  return read_record<std::uint32_t>(
      payload_, element_offset(header_.bone_entries_offset, index, sizeof(std::uint32_t)));
}

VertexStreamLayout MeshAssetView::layout(const MeshGroupRecord& group) const noexcept {
  return layout_of(group);
}

std::span<const std::byte> MeshAssetView::stream_bytes(const MeshGroupRecord& group,
                                                       std::uint32_t stream) const noexcept {
  return stream == 0U ? payload_.subspan(group.stream0_offset, group.stream0_size)
                      : payload_.subspan(group.stream1_offset, group.stream1_size);
}

std::span<const std::byte> MeshAssetView::index_bytes(const MeshGroupRecord& group) const noexcept {
  return payload_.subspan(group.index_offset, group.index_count * kIndexBytes);
}

std::string_view MeshAssetView::skeleton_id() const noexcept {
  if (header_.skeleton_id_length == 0U) {
    return {};
  }
  const std::span<const std::byte> bytes = payload_.subspan(
      static_cast<std::size_t>(header_.strings_offset) + header_.skeleton_id_offset,
      header_.skeleton_id_length);
  // Viewing bytes as characters is the one cast the language allows for
  // this (char may alias any object).
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

MeshAssetFailure decode_mesh_asset(std::span<const std::byte> payload,
                                   MeshAssetView& view) noexcept {
  using E = MeshAssetError;
  const std::uint64_t size = payload.size();
  if (size < sizeof(MeshPayloadHeader)) {
    return {E::HeaderTruncated, 0, VertexStreamError::None};
  }
  const auto h = read_record<MeshPayloadHeader>(payload, 0);
  if (const MeshAssetFailure f = check_sections(h, size); f.error != E::None) {
    return f;
  }
  if (h.group_count == 0U) {
    return {E::NoGroups, 0, VertexStreamError::None};
  }
  if (const MeshAssetFailure f = check_groups(payload, h); f.error != E::None) {
    return f;
  }
  if (const MeshAssetFailure f = check_draws(payload, h); f.error != E::None) {
    return f;
  }
  for (std::uint32_t m = 0; m < h.bone_map_count; ++m) {
    const MeshBoneMapRecord map = read_bone_map(payload, h, m);
    if (!within(map.first_entry, map.entry_count, h.bone_entry_count)) {
      return {E::BoneEntriesOutOfRange, m, VertexStreamError::None};
    }
  }
  if (h.skeleton_id_length != 0U) {
    if (!within(h.skeleton_id_offset, h.skeleton_id_length, h.string_bytes) ||
        content::validate_asset_id(
            payload.subspan(static_cast<std::size_t>(h.strings_offset) + h.skeleton_id_offset,
                            h.skeleton_id_length)) != content::AssetIdError::None) {
      return {E::SkeletonIdInvalid, 0, VertexStreamError::None};
    }
  }
  view.payload_ = payload;
  view.header_ = h;
  return {E::None, 0, VertexStreamError::None};
}

} // namespace bw::geometry
