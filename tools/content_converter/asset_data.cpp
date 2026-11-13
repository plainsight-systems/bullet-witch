#include "bw/content_converter/asset_data.hpp"

#include "bw/animation/skeleton_asset.hpp"
#include "bw/content/asset_id.hpp"
#include "bw/content/cooked_asset.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/morph_targets_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/texture_asset.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Responsibility: the out-of-line part of bw/content_converter/asset_data.hpp:
// the five payload encoders and the cooked frame.
//
// Contract (the header's): each encoder lays the sections out in the order
// its layout header states, every section at the next 16-byte boundary with
// zero bytes between, and then runs the layout's own decoder over the bytes
// it built. A payload the decoder refuses is not returned: the vector comes
// back empty and the decoder's error and index are in `failure`. That makes
// "the same rules the decoder checks" true by construction, with one
// definition of each rule, the decoder's.
//
// Readings of the header that the encoders encode:
//   Data the layout cannot represent is refused before any byte is written,
//   with the decoder error that names the field: a payload or table larger
//   than a u32 offset can address is SectionOutOfBounds; a morph target
//   whose delta bytes are not vertex_count * kMorphDeltaBytes is
//   DeltaSizeMismatch for that target; a one-stream mesh group carrying
//   stream 1 bytes is StreamSizeMismatch for that group (the layout has no
//   place for them). The material decoder names a table and an index; the
//   failure carries both, the table as material_asset.hpp numbers them. The
//   other four layouts have no table and leave it 0.
//   encode_cooked_asset returns an empty vector for the empty id alone: a
//   non-empty AssetId has validated, and the buffer is sized by
//   cooked_payload_offset, so encode_cooked_header cannot refuse either
//   argument; a refusal would be a broken precondition, not an input, and
//   stops the process.
//   String tables hold each string once per use, in record order, with no
//   terminators and no sharing, so equal data gives equal bytes.
namespace bw::content_converter {

namespace {

constexpr std::size_t kAlignment = 16;

// A payload under construction: sections appended at 16-byte boundaries,
// records copied in by memcpy (the layouts' records are trivially copyable
// with no padding).
class Builder {
public:
  // Reserves `bytes` zero bytes at the next boundary and returns the offset.
  std::size_t reserve(std::size_t bytes) {
    pad();
    const std::size_t offset = data_.size();
    data_.resize(offset + bytes);
    return offset;
  }
  std::size_t append(std::span<const std::byte> bytes) {
    const std::size_t offset = reserve(bytes.size());
    std::copy(bytes.begin(), bytes.end(), data_.begin() + static_cast<std::ptrdiff_t>(offset));
    return offset;
  }
  template <class Record> void write(std::size_t offset, const Record& record) {
    static_assert(std::is_trivially_copyable_v<Record>);
    std::memcpy(data_.data() + offset, &record, sizeof(Record));
  }
  void pad() { data_.resize((data_.size() + kAlignment - 1U) / kAlignment * kAlignment); }
  [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }
  // The payload, its last section padded like every other.
  [[nodiscard]] std::vector<std::byte> take() {
    pad();
    return std::move(data_);
  }

private:
  std::vector<std::byte> data_{};
};

// A string table built in record order; offsets are from its start.
class Strings {
public:
  struct Slice {
    std::uint32_t offset{};
    std::uint32_t length{};
  };
  Slice add(std::string_view text) {
    const Slice slice{static_cast<std::uint32_t>(bytes_.size()),
                      static_cast<std::uint32_t>(text.size())};
    for (const char c : text) {
      bytes_.push_back(static_cast<std::byte>(c));
    }
    return slice;
  }
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept { return bytes_; }

private:
  std::vector<std::byte> bytes_{};
};

constexpr std::uint64_t kMaxPayloadBytes = std::numeric_limits<std::uint32_t>::max();

template <class Error>
EncodeFailure failure_of(Error error, std::uint32_t index, std::uint32_t table = 0) noexcept {
  return {static_cast<std::uint32_t>(error), index, table};
}

std::uint32_t u32(std::size_t value) noexcept { return static_cast<std::uint32_t>(value); }

std::vector<std::byte> u16_bytes(const std::vector<std::uint16_t>& values) {
  std::vector<std::byte> out(values.size() * 2U);
  for (std::size_t i = 0; i < values.size(); ++i) {
    out[2U * i] = static_cast<std::byte>(values[i] & 0xFFU);
    out[(2U * i) + 1U] = static_cast<std::byte>(values[i] >> 8U);
  }
  return out;
}

// The bytes a mesh needs, checked against the u32 offsets before building.
std::uint64_t mesh_bytes(const MeshAssetData& data) noexcept {
  std::uint64_t total = sizeof(geometry::MeshPayloadHeader) + kAlignment * 10U;
  total += data.groups.size() * sizeof(geometry::MeshGroupRecord);
  total += data.draws.size() * sizeof(geometry::MeshDrawRecord);
  total += data.bone_maps.size() * sizeof(geometry::MeshBoneMapRecord);
  for (const std::vector<std::uint32_t>& map : data.bone_maps) {
    total += map.size() * sizeof(std::uint32_t);
  }
  total += data.skeleton_id.size();
  total += data.clusters.size() * sizeof(geometry::MeshClusterRecord);
  total += data.levels.size() * sizeof(geometry::MeshLevelRecord);
  for (const MeshGroupData& g : data.groups) {
    total += g.stream0.size() + g.stream1.size() + (g.indices.size() * 2U) + (kAlignment * 3U);
  }
  return total;
}

} // namespace

std::vector<std::byte> encode_mesh_asset(const MeshAssetData& data, EncodeFailure& failure) {
  using E = geometry::MeshAssetError;
  failure = {};
  if (mesh_bytes(data) > kMaxPayloadBytes) {
    failure = failure_of(E::SectionOutOfBounds, 0);
    return {};
  }
  for (std::size_t g = 0; g < data.groups.size(); ++g) {
    if (data.groups[g].layout.stream_count <= 1U && !data.groups[g].stream1.empty()) {
      failure = failure_of(E::StreamSizeMismatch, u32(g));
      return {};
    }
  }
  geometry::MeshPayloadHeader h{};
  h.group_count = u32(data.groups.size());
  h.draw_count = u32(data.draws.size());
  h.material_count = data.material_count;
  h.bone_map_count = u32(data.bone_maps.size());
  std::vector<std::uint32_t> entries;
  std::vector<geometry::MeshBoneMapRecord> maps;
  for (const std::vector<std::uint32_t>& map : data.bone_maps) {
    maps.push_back({u32(entries.size()), u32(map.size())});
    entries.insert(entries.end(), map.begin(), map.end());
  }
  h.bone_entry_count = u32(entries.size());
  Strings strings;
  const Strings::Slice skeleton = strings.add(data.skeleton_id);
  h.string_bytes = u32(strings.bytes().size());
  h.skeleton_id_offset = skeleton.offset;
  h.skeleton_id_length = skeleton.length;
  h.bounds_max = data.bounds_max;
  h.bounds_min = data.bounds_min;
  h.center = data.center;
  h.radius = data.radius;
  h.cluster_count = u32(data.clusters.size());
  h.level_count = u32(data.levels.size());

  Builder out;
  const std::size_t header_at = out.reserve(sizeof(geometry::MeshPayloadHeader));
  h.groups_offset = u32(out.reserve(data.groups.size() * sizeof(geometry::MeshGroupRecord)));
  h.draws_offset = u32(out.append(std::as_bytes(std::span(data.draws))));
  h.bone_maps_offset = u32(out.append(std::as_bytes(std::span(maps))));
  h.bone_entries_offset = u32(out.append(std::as_bytes(std::span(entries))));
  h.strings_offset = u32(out.append(strings.bytes()));
  h.clusters_offset = u32(out.append(std::as_bytes(std::span(data.clusters))));
  h.levels_offset = u32(out.append(std::as_bytes(std::span(data.levels))));
  for (std::size_t g = 0; g < data.groups.size(); ++g) {
    const MeshGroupData& group = data.groups[g];
    const geometry::VertexStreamLayout& layout = group.layout;
    geometry::MeshGroupRecord record{};
    const std::size_t attributes =
        std::min<std::size_t>(layout.attribute_count, geometry::kVertexAttributeCount);
    for (std::size_t i = 0; i < attributes; ++i) {
      const geometry::VertexAttributeDescription& a = layout.attributes[i];
      record.attributes[i] = {static_cast<std::uint8_t>(a.attribute),
                              static_cast<std::uint8_t>(a.type), a.stream, 0, a.offset};
    }
    record.attribute_count = layout.attribute_count;
    record.stream_count = layout.stream_count;
    record.stride0 = layout.streams[0].stride;
    record.stride1 = layout.stream_count > 1U ? layout.streams[1].stride : 0U;
    record.vertex_count = group.vertex_count;
    record.stream0_offset = u32(out.append(group.stream0));
    record.stream0_size = u32(group.stream0.size());
    if (layout.stream_count > 1U) {
      record.stream1_offset = u32(out.append(group.stream1));
      record.stream1_size = u32(group.stream1.size());
    }
    record.index_offset = u32(out.append(u16_bytes(group.indices)));
    record.index_count = u32(group.indices.size());
    record.first_draw = group.first_draw;
    record.draw_count = group.draw_count;
    record.morph_set = group.morph_set;
    out.write(h.groups_offset + (g * sizeof(geometry::MeshGroupRecord)), record);
  }
  out.write(header_at, h);
  std::vector<std::byte> payload = out.take();
  geometry::MeshAssetView view;
  const geometry::MeshAssetFailure decoded = geometry::decode_mesh_asset(payload, view);
  if (decoded.error != E::None) {
    failure = failure_of(decoded.error, decoded.index);
    return {};
  }
  return payload;
}

std::vector<std::byte> encode_morph_targets(const MorphTargetsData& data, EncodeFailure& failure) {
  using E = geometry::MorphTargetsError;
  failure = {};
  const std::uint64_t target_bytes =
      static_cast<std::uint64_t>(data.vertex_count) * geometry::kMorphDeltaBytes;
  std::uint64_t total = sizeof(geometry::MorphTargetsPayloadHeader) + (kAlignment * 4U);
  for (std::size_t t = 0; t < data.targets.size(); ++t) {
    if (data.targets[t].deltas.size() != target_bytes) {
      failure = failure_of(E::DeltaSizeMismatch, u32(t));
      return {};
    }
    total += sizeof(geometry::MorphTargetRecord) + target_bytes + data.targets[t].name.size();
  }
  if (total > kMaxPayloadBytes) {
    failure = failure_of(E::SectionOutOfBounds, 0);
    return {};
  }
  geometry::MorphTargetsPayloadHeader h{};
  h.group = data.group;
  h.vertex_count = data.vertex_count;
  h.target_count = u32(data.targets.size());
  Strings strings;
  std::vector<geometry::MorphTargetRecord> records;
  std::vector<std::byte> deltas;
  for (const MorphTargetData& target : data.targets) {
    const Strings::Slice name = strings.add(target.name);
    records.push_back({name.offset, name.length, 0, 0});
    deltas.insert(deltas.end(), target.deltas.begin(), target.deltas.end());
  }
  h.string_bytes = u32(strings.bytes().size());
  Builder out;
  const std::size_t header_at = out.reserve(sizeof(h));
  h.targets_offset = u32(out.append(std::as_bytes(std::span(records))));
  h.deltas_offset = u32(out.append(deltas));
  h.strings_offset = u32(out.append(strings.bytes()));
  out.write(header_at, h);
  std::vector<std::byte> payload = out.take();
  geometry::MorphTargetsView view;
  const geometry::MorphTargetsFailure decoded = geometry::decode_morph_targets(payload, view);
  if (decoded.error != E::None) {
    failure = failure_of(decoded.error, decoded.index);
    return {};
  }
  return payload;
}

std::vector<std::byte> encode_skeleton_asset(const SkeletonAssetData& data,
                                             EncodeFailure& failure) {
  using E = animation::SkeletonAssetError;
  failure = {};
  std::uint64_t total = sizeof(animation::SkeletonPayloadHeader) + (kAlignment * 3U);
  for (const BoneData& bone : data.bones) {
    total += sizeof(animation::BoneRecord) + bone.name.size();
  }
  if (total > kMaxPayloadBytes) {
    failure = failure_of(E::SectionOutOfBounds, 0);
    return {};
  }
  animation::SkeletonPayloadHeader h{};
  h.bone_count = u32(data.bones.size());
  h.bind_block = static_cast<std::uint32_t>(data.bind_block);
  Strings strings;
  std::vector<animation::BoneRecord> records;
  for (const BoneData& bone : data.bones) {
    const Strings::Slice name = strings.add(bone.name);
    animation::BoneRecord r{};
    r.name_offset = name.offset;
    r.name_length = name.length;
    r.id = bone.id;
    r.parent = bone.parent;
    r.field_12 = bone.field_12;
    r.field_20 = bone.field_20;
    r.a = bone.a;
    r.b = bone.b;
    records.push_back(r);
  }
  h.string_bytes = u32(strings.bytes().size());
  Builder out;
  const std::size_t header_at = out.reserve(sizeof(h));
  h.bones_offset = u32(out.append(std::as_bytes(std::span(records))));
  h.strings_offset = u32(out.append(strings.bytes()));
  out.write(header_at, h);
  std::vector<std::byte> payload = out.take();
  animation::SkeletonAssetView view;
  const animation::SkeletonAssetFailure decoded = animation::decode_skeleton_asset(payload, view);
  if (decoded.error != E::None) {
    failure = failure_of(decoded.error, decoded.index);
    return {};
  }
  return payload;
}

std::vector<std::byte> encode_texture_asset(const TextureAssetData& data, EncodeFailure& failure) {
  using E = rendering::TextureAssetError;
  failure = {};
  std::uint64_t total = sizeof(rendering::TexturePayloadHeader) + (kAlignment * 2U);
  for (const SubresourceData& s : data.subresources) {
    total += sizeof(rendering::SubresourceRecord) + s.bytes.size() + kAlignment;
  }
  if (total > kMaxPayloadBytes) {
    failure = failure_of(E::SectionOutOfBounds, 0);
    return {};
  }
  rendering::TexturePayloadHeader h{};
  h.width = data.width;
  h.height = data.height;
  h.faces = data.faces;
  h.mip_levels = data.mip_levels;
  h.format = static_cast<std::uint32_t>(data.format);
  h.dimension = static_cast<std::uint32_t>(data.dimension);
  h.flags = data.flags;
  h.subresource_count = u32(data.subresources.size());
  Builder out;
  const std::size_t header_at = out.reserve(sizeof(h));
  h.subresources_offset =
      u32(out.reserve(data.subresources.size() * sizeof(rendering::SubresourceRecord)));
  for (std::size_t i = 0; i < data.subresources.size(); ++i) {
    const SubresourceData& s = data.subresources[i];
    const rendering::SubresourceRecord record{u32(out.append(s.bytes)), u32(s.bytes.size()),
                                              s.row_pitch, 0};
    out.write(h.subresources_offset + (i * sizeof(rendering::SubresourceRecord)), record);
  }
  out.write(header_at, h);
  std::vector<std::byte> payload = out.take();
  rendering::TextureAssetView view;
  const rendering::TextureAssetFailure decoded = rendering::decode_texture_asset(payload, view);
  if (decoded.error != E::None) {
    failure = failure_of(decoded.error, decoded.index);
    return {};
  }
  return payload;
}

std::vector<std::byte> encode_material_asset(const MaterialAssetData& data,
                                             EncodeFailure& failure) {
  using E = materials::MaterialAssetError;
  failure = {};
  std::uint64_t total =
      sizeof(materials::MaterialPayloadHeader) + (kAlignment * 6U) + data.template_name.size();
  for (const NamedSwitch& s : data.switches) {
    total += sizeof(materials::SwitchRecord) + s.name.size();
  }
  for (const NamedScalar& s : data.scalars) {
    total += sizeof(materials::ScalarRecord) + s.name.size();
  }
  for (const NamedVector& v : data.vectors) {
    total += sizeof(materials::VectorRecord) + v.name.size();
  }
  for (const NamedTexture& t : data.textures) {
    total += sizeof(materials::TextureRecord) + t.name.size() + t.id.size();
  }
  if (total > kMaxPayloadBytes) {
    failure = failure_of(E::SectionOutOfBounds, 0);
    return {};
  }
  materials::MaterialPayloadHeader h{};
  Strings strings;
  const Strings::Slice template_name = strings.add(data.template_name);
  h.template_name_offset = template_name.offset;
  h.template_name_length = template_name.length;
  std::vector<materials::SwitchRecord> switches;
  for (const NamedSwitch& s : data.switches) {
    const Strings::Slice name = strings.add(s.name);
    switches.push_back({name.offset, name.length, s.value ? 1U : 0U, 0});
  }
  std::vector<materials::ScalarRecord> scalars;
  for (const NamedScalar& s : data.scalars) {
    const Strings::Slice name = strings.add(s.name);
    scalars.push_back({name.offset, name.length, s.value, 0});
  }
  std::vector<materials::VectorRecord> vectors;
  for (const NamedVector& v : data.vectors) {
    const Strings::Slice name = strings.add(v.name);
    vectors.push_back({name.offset, name.length, v.value, 0, 0});
  }
  std::vector<materials::TextureRecord> textures;
  for (const NamedTexture& t : data.textures) {
    const Strings::Slice name = strings.add(t.name);
    const Strings::Slice id = strings.add(t.id);
    textures.push_back({name.offset, name.length, id.offset, id.length});
  }
  h.switch_count = u32(switches.size());
  h.scalar_count = u32(scalars.size());
  h.vector_count = u32(vectors.size());
  h.texture_count = u32(textures.size());
  h.string_bytes = u32(strings.bytes().size());
  Builder out;
  const std::size_t header_at = out.reserve(sizeof(h));
  h.switches_offset = u32(out.append(std::as_bytes(std::span(switches))));
  h.scalars_offset = u32(out.append(std::as_bytes(std::span(scalars))));
  h.vectors_offset = u32(out.append(std::as_bytes(std::span(vectors))));
  h.textures_offset = u32(out.append(std::as_bytes(std::span(textures))));
  h.strings_offset = u32(out.append(strings.bytes()));
  out.write(header_at, h);
  std::vector<std::byte> payload = out.take();
  materials::MaterialAssetView view;
  const materials::MaterialAssetFailure decoded = materials::decode_material_asset(payload, view);
  if (decoded.error != E::None) {
    failure = failure_of(decoded.error, decoded.index, decoded.table);
    return {};
  }
  return payload;
}

std::vector<std::byte> encode_cooked_asset(content::AssetKind kind, const content::AssetId& id,
                                           const content::Hash& input_hash,
                                           std::span<const std::byte> payload) {
  // The default-constructed id is valid for nothing (asset_id.hpp); the
  // frame cannot be written without one, and the result is empty.
  if (id.empty()) {
    return {};
  }
  content::CookedHeaderFields fields{};
  fields.kind = kind;
  fields.payload_size = payload.size();
  fields.input_hash = input_hash;
  fields.payload_hash = content::hash_bytes(payload);
  fields.id = id.view();
  const std::size_t header_bytes = content::cooked_payload_offset(id.view().size());
  std::vector<std::byte> file(header_bytes + payload.size());
  if (content::encode_cooked_header(fields, std::span(file).first(header_bytes)) != header_bytes) {
    std::abort();
  }
  std::copy(payload.begin(), payload.end(),
            file.begin() + static_cast<std::ptrdiff_t>(header_bytes));
  return file;
}

} // namespace bw::content_converter
