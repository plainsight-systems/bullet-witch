// Unit test: public contracts of bw/rendering/scene/scene_geometry.hpp's
// tables. The mesh payload is built here from mesh_asset.hpp's stated
// layout and decoded; no device.
#include "bw/rendering/scene/scene_geometry.hpp"

#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/scene/cluster_records.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace {

namespace g = bw::geometry;
namespace s = bw::scene;
using bw::testing::require;
using Bytes = std::vector<std::byte>;

template <class T> std::uint32_t append(Bytes& b, std::span<const T> records) {
  b.resize((b.size() + 15U) & ~std::size_t{15});
  const auto at = static_cast<std::uint32_t>(b.size());
  const auto bytes = std::as_bytes(records);
  b.insert(b.end(), bytes.begin(), bytes.end());
  return at;
}

// Two groups of 4 vertices (position only): group 0 has 6 indices and draw
// 0 (base vertex 1, 2 clusters), group 1 has 3 indices and draw 1 (1
// cluster); one level.
Bytes mesh_payload() {
  std::array<g::MeshGroupRecord, 2> groups{};
  for (g::MeshGroupRecord& group : groups) {
    group.attributes[0] = {static_cast<std::uint8_t>(g::VertexAttribute::Position),
                           static_cast<std::uint8_t>(g::VertexElementType::Float3), 0, 0, 0};
    group.attribute_count = 1;
    group.stream_count = 1;
    group.stride0 = 12;
    group.vertex_count = 4;
    group.stream0_size = 48;
  }
  groups[0].index_count = 6;
  groups[0].draw_count = 1;
  groups[1].index_count = 3;
  groups[1].first_draw = 1;
  groups[1].draw_count = 1;
  const std::array draws{g::MeshDrawRecord{1, 0, 3, 0, 6, g::kNoMaterial, g::kNoBoneMap, 0},
                         g::MeshDrawRecord{0, 0, 3, 0, 3, g::kNoMaterial, g::kNoBoneMap, 0}};
  const std::array clusters{
      g::MeshClusterRecord{0, 3, 0, g::kNoClusterBone, {0.0F, 0.0F, 0.0F}, {2.0F, 2.0F, 2.0F}},
      g::MeshClusterRecord{3, 3, 0, g::kNoClusterBone, {-2.0F, 1.0F, 1.0F}, {1.0F, 4.0F, 1.0F}},
      g::MeshClusterRecord{0, 3, 1, g::kNoClusterBone, {5.0F, 5.0F, 5.0F}, {6.0F, 6.0F, 6.0F}}};
  const std::array levels{g::MeshLevelRecord{0, 3, 1.0F, 0}};
  const std::array<float, 12> positions{};
  const std::array<std::uint16_t, 6> indices{0, 1, 2, 2, 1, 0};
  g::MeshPayloadHeader h{};
  h.group_count = 2;
  h.draw_count = 2;
  h.cluster_count = 3;
  h.level_count = 1;
  Bytes b(sizeof(h));
  h.draws_offset = append<g::MeshDrawRecord>(b, draws);
  h.clusters_offset = append<g::MeshClusterRecord>(b, clusters);
  h.levels_offset = append<g::MeshLevelRecord>(b, levels);
  for (g::MeshGroupRecord& group : groups) {
    group.stream0_offset = append<float>(b, positions);
    group.index_offset = append<std::uint16_t>(b, std::span(indices).first(group.index_count));
  }
  h.groups_offset = append<g::MeshGroupRecord>(b, groups);
  b.resize((b.size() + 15U) & ~std::size_t{15});
  std::memcpy(b.data(), &h, sizeof(h));
  return b;
}

// (scene_geometry.hpp, contract 2): a mesh added after another has its
// clusters' first_index moved to its groups' place in the index buffer and
// its level runs moved past the earlier clusters, one level record per draw;
// each draw's geometry names its level records, its clusters, its group's
// block with its base vertex and the union of its level-0 boxes; a table
// without room is Full, names it and appends nothing.
void rebases_tables() {
  const Bytes payload = mesh_payload();
  g::MeshAssetView mesh;
  require(g::decode_mesh_asset(payload, mesh).error == g::MeshAssetError::None, "the mesh decodes");
  std::array<g::VertexStreams, 2> blocks{};
  blocks[0].buffer0 = {11};
  blocks[1].buffer0 = {22};
  s::SceneGeometry geometry(s::SceneGeometryCapacity{6, 100, 100});
  s::SceneMesh first;
  s::SceneMesh second;
  require(geometry.add_mesh(mesh, blocks, first).ok() &&
              geometry.add_mesh(mesh, blocks, second).ok(),
          "two copies fit");
  const std::array<std::uint32_t, 3> first_index{9, 12, 15};
  for (std::size_t c = 0; c < 3U; ++c) {
    require(geometry.clusters()[3U + c].first_index == first_index[c], c,
            "first_index at its group's place");
  }
  require(geometry.levels().size() == 4U && geometry.levels()[2].first_cluster == 3U &&
              geometry.levels()[2].cluster_count == 2U &&
              geometry.levels()[3].first_cluster == 5U && geometry.levels()[3].cluster_count == 1U,
          "one level record per draw, its run past the earlier clusters");
  const s::InstanceGeometry& draw0 = second.draws[0];
  require(draw0.first_level == 2U && draw0.level_count == 1U && draw0.cluster_count == 2U &&
              draw0.streams.buffer0.value == 11U && draw0.streams.base_vertex == 1U &&
              second.draws[1].streams.buffer0.value == 22U,
          "the draw's levels, clusters and group block with its base vertex");
  require(draw0.bounds.min.x == -2.0F && draw0.bounds.min.y == 0.0F && draw0.bounds.max.y == 4.0F &&
              draw0.bounds.max.z == 2.0F,
          "the union of its level-0 boxes");
  s::SceneMesh third;
  const s::GeometryFailure full = geometry.add_mesh(mesh, blocks, third);
  require(full.error == s::GeometryError::Full && full.table == s::GeometryTable::Clusters &&
              geometry.clusters().size() == 6U && geometry.levels().size() == 4U &&
              geometry.indices().size() == 18U,
          "a full table is named and nothing is appended");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{Scenario{"rebases_tables", &rebases_tables}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
