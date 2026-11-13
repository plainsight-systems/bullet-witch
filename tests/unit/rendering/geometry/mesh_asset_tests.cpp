// Unit test: public contracts of bw/rendering/geometry/mesh_asset.hpp and
// morph_targets_asset.hpp; each scenario names the declaration it asserts. Payloads are built here
// by copying the headers' records to the 16-aligned offsets they state.
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/morph_targets_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace g = bw::geometry;
using bw::testing::require;

using Bytes = std::vector<std::byte>;

void pad16(Bytes& bytes) { bytes.resize((bytes.size() + 15U) & ~std::size_t{15}); }

template <class T> T read_at(const Bytes& bytes, std::size_t offset) {
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}
template <class T> void write_at(Bytes& bytes, std::size_t offset, const T& value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(T));
}
// Appends at the next 16-byte boundary and returns the offset.
std::uint32_t append(Bytes& bytes, std::span<const std::byte> data) {
  pad16(bytes);
  const auto offset = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), data.begin(), data.end());
  return offset;
}
template <class T> std::uint32_t append_records(Bytes& bytes, const std::vector<T>& records) {
  return append(bytes, std::as_bytes(std::span(records)));
}
// `count` bytes counting up from `first`, so a span over the wrong bytes is
// seen.
Bytes filled(std::size_t count, std::uint8_t first) {
  Bytes bytes(count);
  for (std::size_t i = 0; i < count; ++i) {
    bytes[i] = static_cast<std::byte>(first + i);
  }
  return bytes;
}
bool same_bytes(std::span<const std::byte> a, std::span<const std::byte> b) {
  return std::ranges::equal(a, b);
}
bool views_into(std::span<const std::byte> part, const Bytes& whole) {
  return part.data() >= whole.data() && part.data() + part.size() <= whole.data() + whole.size();
}

g::MeshAttributeRecord attribute(g::VertexAttribute a, g::VertexElementType t, std::uint8_t stream,
                                 std::uint32_t offset) {
  return {static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(t), stream, 0, offset};
}

// The minimal mesh: group 0 has two streams (position float3 in stream 0 at
// stride 12; normal short4n at 0 and texture coordinate 1 half2 at 8 in
// stream 1 at stride 12), 4 vertices, 6 indices, one draw with material 0
// and bone map 0, morph set 0. Group 1 has one stream (position float3,
// bone slots ubyte4 at 12, bone weights ubyte4n at 16, stride 20), 3
// vertices, 3 indices, and two draws: material 1 with bone map 1, and no
// material and no bone map at base vertex 1 ending exactly at the group's
// last vertex. Two bone maps over three entries; skeleton
// "model.pw0.mesh.skeleton". Four clusters: draw 0's two triangles, draw 1
// rigid to bone 7, draw 2; level 0 holds the first three, level 1 the last.
struct Mesh {
  std::vector<g::MeshGroupRecord> groups;
  std::vector<g::MeshDrawRecord> draws;
  std::vector<g::MeshBoneMapRecord> maps;
  std::vector<std::uint32_t> entries;
  std::vector<g::MeshClusterRecord> clusters;
  std::vector<g::MeshLevelRecord> levels;
  std::array<Bytes, 3> streams;
  std::array<Bytes, 2> indices;
  std::string skeleton;
  Bytes bytes;
};

constexpr std::string_view kSkeleton = "model.pw0.mesh.skeleton";

Mesh make_mesh() {
  using A = g::VertexAttribute;
  using T = g::VertexElementType;
  Mesh m;
  g::MeshGroupRecord g0{};
  g0.attributes[0] = attribute(A::Position, T::Float3, 0, 0);
  g0.attributes[1] = attribute(A::Normal, T::Short4Norm, 1, 0);
  g0.attributes[2] = attribute(A::TexCoord0, T::Half2, 1, 8);
  g0.attribute_count = 3;
  g0.stream_count = 2;
  g0.stride0 = 12;
  g0.stride1 = 12;
  g0.vertex_count = 4;
  g0.index_count = 6;
  g0.first_draw = 0;
  g0.draw_count = 1;
  g0.morph_set = 0;
  g::MeshGroupRecord g1{};
  g1.attributes[0] = attribute(A::Position, T::Float3, 0, 0);
  g1.attributes[1] = attribute(A::BoneSlots, T::UByte4, 0, 12);
  g1.attributes[2] = attribute(A::BoneWeights, T::UByte4Norm, 0, 16);
  g1.attribute_count = 3;
  g1.stream_count = 1;
  g1.stride0 = 20;
  g1.vertex_count = 3;
  g1.index_count = 3;
  g1.first_draw = 1;
  g1.draw_count = 2;
  g1.morph_set = g::kNoMorphSet;
  m.groups = {g0, g1};
  m.draws = {
      g::MeshDrawRecord{0, 0, 4, 0, 6, 0, 0, 0},
      g::MeshDrawRecord{0, 0, 3, 0, 3, 1, 1, 0},
      g::MeshDrawRecord{1, 0, 2, 0, 3, g::kNoMaterial, g::kNoBoneMap, 0},
  };
  m.maps = {g::MeshBoneMapRecord{0, 2}, g::MeshBoneMapRecord{2, 1}};
  m.entries = {5, 7, 9};
  m.clusters = {
      g::MeshClusterRecord{0, 3, 0, g::kNoClusterBone, {0.0F, 1.0F, 2.0F}, {3.0F, 4.0F, 5.0F}},
      g::MeshClusterRecord{3, 3, 0, g::kNoClusterBone, {6.0F, 7.0F, 8.0F}, {9.0F, 10.0F, 11.0F}},
      g::MeshClusterRecord{0, 3, 1, 7, {-1.0F, -2.0F, -3.0F}, {1.0F, 2.0F, 3.0F}},
      g::MeshClusterRecord{0, 3, 2, g::kNoClusterBone, {-4.0F, -5.0F, -6.0F}, {4.0F, 5.0F, 6.0F}},
  };
  m.levels = {g::MeshLevelRecord{0, 3, 1.0F, 0}, g::MeshLevelRecord{3, 1, 0.25F, 0}};
  m.streams = {filled(48, 1), filled(48, 64), filled(60, 128)};
  m.indices = {filled(12, 200), filled(6, 220)};
  m.skeleton = std::string(kSkeleton);
  return m;
}

// Header at 0, then groups, draws, bone maps, bone entries, the string
// table, clusters, levels, and each group's stream and index bytes, each at the next 16-byte
// boundary; the records' offsets and the header's counts are filled here.
void lay_out(Mesh& m) {
  g::MeshPayloadHeader h{};
  h.group_count = static_cast<std::uint32_t>(m.groups.size());
  h.draw_count = static_cast<std::uint32_t>(m.draws.size());
  h.material_count = 2;
  h.bone_map_count = static_cast<std::uint32_t>(m.maps.size());
  h.bone_entry_count = static_cast<std::uint32_t>(m.entries.size());
  h.string_bytes = static_cast<std::uint32_t>(m.skeleton.size());
  h.skeleton_id_offset = 0;
  h.skeleton_id_length = h.string_bytes;
  h.bounds_max = {1.0F, 2.0F, 3.0F};
  h.bounds_min = {-1.0F, -2.0F, -3.0F};
  h.center = {0.0F, 0.5F, 0.0F};
  h.radius = 4.0F;
  h.cluster_count = static_cast<std::uint32_t>(m.clusters.size());
  h.level_count = static_cast<std::uint32_t>(m.levels.size());
  Bytes& b = m.bytes;
  b.assign(sizeof(g::MeshPayloadHeader), std::byte{});
  h.groups_offset = append_records(b, m.groups);
  h.draws_offset = append_records(b, m.draws);
  h.bone_maps_offset = append_records(b, m.maps);
  h.bone_entries_offset = append_records(b, m.entries);
  h.strings_offset = append(b, std::as_bytes(std::span(m.skeleton)));
  h.clusters_offset = append_records(b, m.clusters);
  h.levels_offset = append_records(b, m.levels);
  m.groups[0].stream0_offset = append(b, m.streams[0]);
  m.groups[0].stream0_size = static_cast<std::uint32_t>(m.streams[0].size());
  m.groups[0].stream1_offset = append(b, m.streams[1]);
  m.groups[0].stream1_size = static_cast<std::uint32_t>(m.streams[1].size());
  m.groups[1].stream0_offset = append(b, m.streams[2]);
  m.groups[1].stream0_size = static_cast<std::uint32_t>(m.streams[2].size());
  m.groups[0].index_offset = append(b, m.indices[0]);
  m.groups[1].index_offset = append(b, m.indices[1]);
  pad16(b);
  for (std::size_t i = 0; i < m.groups.size(); ++i) {
    write_at(b, h.groups_offset + (i * sizeof(g::MeshGroupRecord)), m.groups[i]);
  }
  write_at(b, 0, h);
}

g::MeshPayloadHeader mesh_header(const Bytes& b) { return read_at<g::MeshPayloadHeader>(b, 0); }
void patch_header(Bytes& b, const std::function<void(g::MeshPayloadHeader&)>& change) {
  auto h = mesh_header(b);
  change(h);
  write_at(b, 0, h);
}
template <class T>
void patch(Bytes& b, std::uint32_t g::MeshPayloadHeader::* section, std::uint32_t index,
           const std::function<void(T&)>& change) {
  const std::size_t offset = mesh_header(b).*section + (std::size_t{index} * sizeof(T));
  auto record = read_at<T>(b, offset);
  change(record);
  write_at(b, offset, record);
}
void patch_group(Bytes& b, std::uint32_t i, const std::function<void(g::MeshGroupRecord&)>& c) {
  patch<g::MeshGroupRecord>(b, &g::MeshPayloadHeader::groups_offset, i, c);
}
void patch_draw(Bytes& b, std::uint32_t i, const std::function<void(g::MeshDrawRecord&)>& c) {
  patch<g::MeshDrawRecord>(b, &g::MeshPayloadHeader::draws_offset, i, c);
}

void require_mesh_reads_back(const Mesh& m) {
  g::MeshAssetView view;
  const g::MeshAssetFailure failure = g::decode_mesh_asset(m.bytes, view);
  require(failure.error == g::MeshAssetError::None, "the minimal mesh decodes");
  require(view.group_count() == 2U && view.draw_count() == 3U && view.bone_map_count() == 2U &&
              view.header().material_count == 2U && view.header().radius == 4.0F &&
              view.header().bounds_max == std::array{1.0F, 2.0F, 3.0F} &&
              view.header().bounds_min == std::array{-1.0F, -2.0F, -3.0F} &&
              view.header().center == std::array{0.0F, 0.5F, 0.0F},
          "counts and bounds read back as stored");
  require(view.payload().data() == m.bytes.data() && view.payload().size() == m.bytes.size(),
          "the view borrows the payload");
  for (std::uint32_t i = 0; i < 2U; ++i) {
    const g::MeshGroupRecord group = view.group(i);
    require(std::memcmp(&group, &m.groups[i], sizeof(group)) == 0, i, "group record reads back");
  }
  for (std::uint32_t i = 0; i < 3U; ++i) {
    const g::MeshDrawRecord draw = view.draw(i);
    require(std::memcmp(&draw, &m.draws[i], sizeof(draw)) == 0, i, "draw record reads back");
  }
  const g::MeshBoneMapRecord map = view.bone_map(1);
  require(map.first_entry == 2U && map.entry_count == 1U, "bone map reads back");
  require(view.bone_entry(0) == 5U && view.bone_entry(1) == 7U && view.bone_entry(2) == 9U,
          "bone entries read back");
  require(view.skeleton_id() == kSkeleton, "skeleton id reads back");

  using A = g::VertexAttribute;
  using T = g::VertexElementType;
  g::VertexStreamLayout expected{};
  expected.attributes[0] = {A::Position, T::Float3, 0, 0};
  expected.attributes[1] = {A::Normal, T::Short4Norm, 1, 0};
  expected.attributes[2] = {A::TexCoord0, T::Half2, 1, 8};
  expected.streams = {g::VertexStreamDescription{12}, g::VertexStreamDescription{12}};
  expected.attribute_count = 3;
  expected.stream_count = 2;
  require(view.layout(view.group(0)) == expected, "group 0's layout is its record's");

  const g::MeshGroupRecord g0 = view.group(0);
  const g::MeshGroupRecord g1 = view.group(1);
  const std::array streams{view.stream_bytes(g0, 0), view.stream_bytes(g0, 1),
                           view.stream_bytes(g1, 0)};
  for (std::size_t s = 0; s < streams.size(); ++s) {
    require(same_bytes(streams[s], m.streams[s]) && views_into(streams[s], m.bytes), s,
            "stream bytes view the payload's own bytes");
  }
  require(same_bytes(view.index_bytes(g0), m.indices[0]) &&
              same_bytes(view.index_bytes(g1), m.indices[1]) &&
              views_into(view.index_bytes(g1), m.bytes),
          "index bytes view the payload's own bytes");
}

// (mesh_asset.hpp, decode_mesh_asset, MeshAssetError order, MeshAssetView):
// the minimal mesh decodes and reads back its records, bounds, layout,
// stream and index bytes, bone entries and skeleton id; a mesh with no
// skeleton reads back an empty id; each row then breaks one rule, or two
// where the order decides, and names the first failure with the group, draw
// or bone map index, and the layout error where the layout is refused.
void rejections_named() {
  Mesh mesh = make_mesh();
  lay_out(mesh);
  require_mesh_reads_back(mesh);
  {
    Bytes bytes = mesh.bytes;
    patch_header(bytes, [](g::MeshPayloadHeader& h) {
      h.skeleton_id_length = 0;
      h.skeleton_id_offset = 0xFFFFU;
    });
    g::MeshAssetView view;
    require(g::decode_mesh_asset(bytes, view).error == g::MeshAssetError::None &&
                view.skeleton_id().empty(),
            "an empty skeleton id is no skeleton");
  }

  using E = g::MeshAssetError;
  using H = g::MeshPayloadHeader;
  using G = g::MeshGroupRecord;
  using D = g::MeshDrawRecord;
  using V = g::VertexStreamError;
  struct Row {
    std::string_view note;
    std::function<void(Bytes&)> change;
    E expected;
    std::uint32_t index;
    V layout_error;
  };
  const std::array rows{
      Row{"header one byte short", [](Bytes& b) { b.resize(sizeof(H) - 1U); }, E::HeaderTruncated,
          0, V::None},
      Row{"string table off 16",
          [](Bytes& b) { patch_header(b, [](H& h) { h.strings_offset += 4U; }); },
          E::SectionMisaligned, 0, V::None},
      Row{"bone entries past the end",
          [](Bytes& b) { patch_header(b, [](H& h) { h.bone_entry_count = 0x40000000U; }); },
          E::SectionOutOfBounds, 0, V::None},
      Row{"no groups", [](Bytes& b) { patch_header(b, [](H& h) { h.group_count = 0; }); },
          E::NoGroups, 0, V::None},
      Row{"group 0 names element type 5",
          [](Bytes& b) { patch_group(b, 0, [](G& r) { r.attributes[2].type = 5; }); },
          E::AttributeUnknown, 0, V::None},
      Row{"group 0 names normal twice",
          [](Bytes& b) {
            patch_group(b, 0, [](G& r) {
              r.attributes[2].attribute = static_cast<std::uint8_t>(g::VertexAttribute::Normal);
            });
          },
          E::LayoutInvalid, 0, V::DuplicateAttribute},
      Row{"group 1 with no vertices",
          [](Bytes& b) { patch_group(b, 1, [](G& r) { r.vertex_count = 0; }); }, E::VertexCountZero,
          1, V::None},
      Row{"group 0's stream 1 four bytes short",
          [](Bytes& b) { patch_group(b, 0, [](G& r) { r.stream1_size -= 4U; }); },
          E::StreamSizeMismatch, 0, V::None},
      Row{"group 0's stream 1 off 16",
          [](Bytes& b) { patch_group(b, 0, [](G& r) { r.stream1_offset += 4U; }); },
          E::StreamOutOfBounds, 0, V::None},
      Row{"group 0 with 4 indices",
          [](Bytes& b) { patch_group(b, 0, [](G& r) { r.index_count = 4; }); },
          E::IndexCountInvalid, 0, V::None},
      Row{"group 1's indices off 16",
          [](Bytes& b) { patch_group(b, 1, [](G& r) { r.index_offset += 2U; }); },
          E::IndexOutOfBounds, 1, V::None},
      Row{"group 1's draws not following group 0's",
          [](Bytes& b) { patch_group(b, 1, [](G& r) { r.first_draw = 2; }); }, E::DrawRangeInvalid,
          1, V::None},
      Row{"draw 2's indices past its group's",
          [](Bytes& b) { patch_draw(b, 2, [](D& r) { r.first_index = 3; }); },
          E::DrawIndexRangeInvalid, 2, V::None},
      Row{"draw 2's vertices past its group's",
          [](Bytes& b) { patch_draw(b, 2, [](D& r) { r.base_vertex = 2; }); },
          E::DrawVertexRangeInvalid, 2, V::None},
      Row{"draw 1's material at material_count",
          [](Bytes& b) { patch_draw(b, 1, [](D& r) { r.material = 2; }); }, E::MaterialOutOfRange,
          1, V::None},
      Row{"draw 0's bone map at bone_map_count",
          [](Bytes& b) { patch_draw(b, 0, [](D& r) { r.bone_map = 2; }); }, E::BoneMapOutOfRange, 0,
          V::None},
      Row{"bone map 1 past the entries",
          [](Bytes& b) {
            patch<g::MeshBoneMapRecord>(b, &H::bone_maps_offset, 1,
                                        [](g::MeshBoneMapRecord& r) { r.entry_count = 2; });
          },
          E::BoneEntriesOutOfRange, 1, V::None},
      Row{"a skeleton id that does not validate (trailing separator)",
          [](Bytes& b) { patch_header(b, [](H& h) { h.skeleton_id_length = 15; }); },
          E::SkeletonIdInvalid, 0, V::None},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    Bytes bytes = mesh.bytes;
    rows[i].change(bytes);
    g::MeshAssetView view;
    const g::MeshAssetFailure failure = g::decode_mesh_asset(bytes, view);
    require(failure.error == rows[i].expected && failure.index == rows[i].index &&
                failure.layout_error == rows[i].layout_error,
            i, rows[i].note);
  }
}

// (mesh_asset.hpp, rules 10 and 11, MeshAssetView::cluster, level,
// cluster_count, level_count): the minimal mesh's cluster and level records
// read back as stored; each row breaks one rule of the two and names its
// error with the cluster or level index.
void cluster_tables() {
  Mesh mesh = make_mesh();
  lay_out(mesh);
  g::MeshAssetView view;
  require(g::decode_mesh_asset(mesh.bytes, view).error == g::MeshAssetError::None &&
              view.cluster_count() == 4U && view.level_count() == 2U,
          "the minimal mesh decodes with its table counts");
  for (std::uint32_t c = 0; c < 4U; ++c) {
    const g::MeshClusterRecord cluster = view.cluster(c);
    const g::MeshClusterRecord& stored = mesh.clusters[c];
    require(cluster.first_index == stored.first_index &&
                cluster.index_count == stored.index_count && cluster.draw == stored.draw &&
                cluster.bone == stored.bone && cluster.box_min == stored.box_min &&
                cluster.box_max == stored.box_max,
            c, "cluster record reads back");
  }
  for (std::uint32_t l = 0; l < 2U; ++l) {
    const g::MeshLevelRecord level = view.level(l);
    require(level.first_cluster == mesh.levels[l].first_cluster &&
                level.cluster_count == mesh.levels[l].cluster_count &&
                level.screen_size == mesh.levels[l].screen_size,
            l, "level record reads back");
  }
  using E = g::MeshAssetError;
  using H = g::MeshPayloadHeader;
  using C = g::MeshClusterRecord;
  const auto patch_cluster = [](Bytes& b, std::uint32_t i, const std::function<void(C&)>& c) {
    patch<C>(b, &H::clusters_offset, i, c);
  };
  struct Row {
    std::string_view note;
    std::function<void(Bytes&)> change;
    E expected;
    std::uint32_t index;
  };
  const std::array rows{
      Row{"cluster 3 names draw 3 of 3",
          [&](Bytes& b) { patch_cluster(b, 3, [](C& r) { r.draw = 3; }); },
          E::ClusterDrawOutOfRange, 3},
      Row{"cluster 1 runs past draw 0's indices",
          [&](Bytes& b) { patch_cluster(b, 1, [](C& r) { r.first_index = 4; }); },
          E::ClusterIndexRangeInvalid, 1},
      Row{"cluster 3 names a bone, its draw has no bone map",
          [&](Bytes& b) { patch_cluster(b, 3, [](C& r) { r.bone = 1; }); },
          E::ClusterBoneWithoutMap, 3},
      Row{"level 1 starts inside level 0",
          [](Bytes& b) {
            patch<g::MeshLevelRecord>(b, &H::levels_offset, 1,
                                      [](g::MeshLevelRecord& r) { r.first_cluster = 2; });
          },
          E::LevelRangeInvalid, 1},
      Row{"level 0's draws decrease at cluster 1",
          [&](Bytes& b) { patch_cluster(b, 0, [](C& r) { r.draw = 1; }); }, E::ClusterOrderInvalid,
          1},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    Bytes bytes = mesh.bytes;
    rows[i].change(bytes);
    const g::MeshAssetFailure failure = g::decode_mesh_asset(bytes, view);
    require(failure.error == rows[i].expected && failure.index == rows[i].index, i, rows[i].note);
  }
}

// Morph targets: group 3, 2 vertices, targets "blink" and "smile", each 16
// delta bytes; header, records, deltas, strings at 16-byte boundaries.
struct Morphs {
  Bytes bytes;
  std::array<Bytes, 2> deltas;
};

Morphs make_morphs() {
  Morphs m;
  m.deltas = {filled(16, 1), filled(16, 100)};
  const std::string strings = "blinksmile";
  g::MorphTargetsPayloadHeader h{};
  h.group = 3;
  h.vertex_count = 2;
  h.target_count = 2;
  h.string_bytes = static_cast<std::uint32_t>(strings.size());
  const std::vector targets{g::MorphTargetRecord{0, 5, 0, 0}, g::MorphTargetRecord{5, 5, 0, 0}};
  Bytes& b = m.bytes;
  b.assign(sizeof(h), std::byte{});
  h.targets_offset = append_records(b, targets);
  h.deltas_offset = append(b, m.deltas[0]);
  b.insert(b.end(), m.deltas[1].begin(), m.deltas[1].end());
  h.strings_offset = append(b, std::as_bytes(std::span(strings)));
  pad16(b);
  write_at(b, 0, h);
  return m;
}

// (morph_targets_asset.hpp, decode_morph_targets, MorphTargetsError order,
// MorphTargetsView): a valid set reads back its group, vertex count, names
// and each target's delta bytes over the payload; each row then breaks one
// rule, or two where the order decides, and names the first failure with
// the target index where the failure is a target's.
void morph_targets_rejections_named() {
  const Morphs morphs = make_morphs();
  {
    g::MorphTargetsView view;
    require(g::decode_morph_targets(morphs.bytes, view).error == g::MorphTargetsError::None,
            "the valid set decodes");
    require(view.header().group == 3U && view.header().vertex_count == 2U &&
                view.target_count() == 2U,
            "group, vertex count and target count read back");
    require(view.name(0) == "blink" && view.name(1) == "smile", "names read back");
    for (std::uint32_t t = 0; t < 2U; ++t) {
      require(same_bytes(view.deltas(t), morphs.deltas[t]) &&
                  views_into(view.deltas(t), morphs.bytes),
              t, "deltas view the target's own bytes");
    }
  }
  using E = g::MorphTargetsError;
  using H = g::MorphTargetsPayloadHeader;
  using R = g::MorphTargetRecord;
  const auto patch_header_m = [](Bytes& b, const std::function<void(H&)>& change) {
    auto h = read_at<H>(b, 0);
    change(h);
    write_at(b, 0, h);
  };
  const auto patch_target = [](Bytes& b, std::uint32_t t, const std::function<void(R&)>& change) {
    const std::size_t offset = read_at<H>(b, 0).targets_offset + (std::size_t{t} * sizeof(R));
    auto r = read_at<R>(b, offset);
    change(r);
    write_at(b, offset, r);
  };
  constexpr std::uint32_t kAny = 0xFFFFFFFFU;
  struct Row {
    std::string_view note;
    std::function<void(Bytes&)> change;
    E expected;
    std::uint32_t index;
  };
  const std::array rows{
      Row{"header one byte short", [](Bytes& b) { b.resize(sizeof(H) - 1U); }, E::HeaderTruncated,
          kAny},
      Row{"deltas off 16",
          [&](Bytes& b) { patch_header_m(b, [](H& h) { h.deltas_offset += 8U; }); },
          E::SectionMisaligned, kAny},
      Row{"target records past the end",
          [&](Bytes& b) { patch_header_m(b, [](H& h) { h.target_count = 0x10000000U; }); },
          E::SectionOutOfBounds, kAny},
      Row{"no vertices", [&](Bytes& b) { patch_header_m(b, [](H& h) { h.vertex_count = 0; }); },
          E::CountZero, kAny},
      Row{"deltas running past the end",
          [&](Bytes& b) { patch_header_m(b, [](H& h) { h.vertex_count = 0x1000U; }); },
          E::DeltaSizeMismatch, kAny},
      Row{"a name past the string table",
          [&](Bytes& b) { patch_target(b, 0, [](R& r) { r.name_offset = 6; }); }, E::NameInvalid,
          0},
      Row{"a name used twice",
          [&](Bytes& b) { patch_target(b, 1, [](R& r) { r.name_offset = 0; }); }, E::NameDuplicate,
          1},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    Bytes bytes = morphs.bytes;
    rows[i].change(bytes);
    g::MorphTargetsView view;
    const g::MorphTargetsFailure failure = g::decode_morph_targets(bytes, view);
    require(failure.error == rows[i].expected &&
                (rows[i].index == kAny || failure.index == rows[i].index),
            i, rows[i].note);
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"rejections_named", &rejections_named},
    Scenario{"cluster_tables", &cluster_tables},
    Scenario{"morph_targets_rejections_named", &morph_targets_rejections_named},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
