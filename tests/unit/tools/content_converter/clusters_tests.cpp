// Unit test: public contracts of bw/content_converter/clusters.hpp; each
// scenario names the declaration it asserts. The mesh is built in memory.
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/clusters.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string_view>

namespace {

namespace cc = bw::content_converter;
namespace g = bw::geometry;
using bw::testing::require;

// One group of 903 vertices: position (v, -v, v / 2) at 0, bone slots at 12,
// bone weights at 16, stride 20. Vertices 0 to 899 put their whole weight on
// slot 1, which holds 2; 900 to 902 split theirs over two slots. Draw 0 is
// indices 0 to 899, naming vertices 899 down to 0, with bone map 0; draw 1
// is indices 0, 1, 2 at base vertex 900 with bone map 0.
cc::MeshAssetData mesh_data() {
  using A = g::VertexAttribute;
  using T = g::VertexElementType;
  cc::MeshGroupData group;
  group.layout.attributes[0] = {A::Position, T::Float3, 0, 0};
  group.layout.attributes[1] = {A::BoneSlots, T::UByte4, 0, 12};
  group.layout.attributes[2] = {A::BoneWeights, T::UByte4Norm, 0, 16};
  group.layout.attribute_count = 3;
  group.layout.stream_count = 1;
  group.layout.streams[0].stride = 20;
  group.vertex_count = 903;
  group.stream0.resize(std::size_t{903} * 20U);
  for (std::size_t v = 0; v < 903U; ++v) {
    const auto x = static_cast<float>(v);
    const std::array<float, 3> position{x, -x, x / 2.0F};
    const std::array<std::uint8_t, 8> bones =
        v < 900U ? std::array<std::uint8_t, 8>{0, 2, 0, 0, 0, 255, 0, 0}
                 : std::array<std::uint8_t, 8>{0, 2, 0, 0, 128, 127, 0, 0};
    std::memcpy(&group.stream0[v * 20U], position.data(), 12);
    std::memcpy(&group.stream0[(v * 20U) + 12U], bones.data(), 8);
  }
  for (std::uint32_t i = 0; i < 900U; ++i) {
    group.indices.push_back(static_cast<std::uint16_t>(899U - i));
  }
  group.indices.insert(group.indices.end(), {0, 1, 2});
  group.draw_count = 2;
  cc::MeshAssetData m;
  m.groups = {group};
  m.draws = {{0, 0, 900, 0, 900, 0, 0, 0}, {900, 0, 3, 900, 3, 0, 0, 0}};
  m.bone_maps = {{10, 11, 12}, {10}};
  return m;
}

// (clusters.hpp, build_clusters, contracts 1 to 5): 300 triangles become
// runs of 128, 128 and 44 in index order and 1 triangle one run; each box
// is its named vertices' extent, base vertex included; the rigid draw's
// clusters carry its map entry for the slot and the split draw's none; one
// level holds every cluster; each error row names its draw and empties both
// tables.
void split_and_boxes() {
  cc::MeshAssetData built = mesh_data();
  require(build_clusters(built).ok(), "the mesh builds");
  struct Expected {
    std::uint32_t first_index, index_count, draw, bone;
    std::array<float, 3> min, max;
  };
  constexpr std::uint32_t kNone = g::kNoClusterBone;
  const std::array expected{
      Expected{0, 384, 0, 12, {516.0F, -899.0F, 258.0F}, {899.0F, -516.0F, 449.5F}},
      Expected{384, 384, 0, 12, {132.0F, -515.0F, 66.0F}, {515.0F, -132.0F, 257.5F}},
      Expected{768, 132, 0, 12, {0.0F, -131.0F, 0.0F}, {131.0F, 0.0F, 65.5F}},
      Expected{900, 3, 1, kNone, {900.0F, -902.0F, 450.0F}, {902.0F, -900.0F, 451.0F}},
  };
  require(built.clusters.size() == expected.size(), "four clusters");
  for (std::size_t c = 0; c < expected.size(); ++c) {
    const g::MeshClusterRecord& x = built.clusters[c];
    const Expected& y = expected[c];
    require(x.first_index == y.first_index && x.index_count == y.index_count && x.draw == y.draw, c,
            "the run's range and draw");
    require(x.bone == y.bone, c, "the bone");
    require(x.box_min == y.min && x.box_max == y.max, c, "the box of its vertices");
  }
  require(built.levels.size() == 1U && built.levels[0].first_cluster == 0U &&
              built.levels[0].cluster_count == 4U &&
              built.levels[0].screen_size == cc::kSingleLevelScreenSize,
          "one level of every cluster");

  struct Row {
    std::string_view note;
    std::function<void(cc::MeshAssetData&)> change;
    cc::ClusterError error;
    std::uint32_t draw;
  };
  const std::array rows{
      Row{"position stored as short4n",
          [](cc::MeshAssetData& m) {
            m.groups[0].layout.attributes[0].type = g::VertexElementType::Short4Norm;
          },
          cc::ClusterError::PositionMissing, 0},
      Row{"draw 1's last vertex at the group's count",
          [](cc::MeshAssetData& m) { m.draws[1].base_vertex = 901; },
          cc::ClusterError::DrawRangeInvalid, 1},
      Row{"draw 0's slot 2 past bone map 1's one entry",
          [](cc::MeshAssetData& m) { m.draws[0].bone_map = 1; }, cc::ClusterError::SlotOutOfRange,
          0},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    cc::MeshAssetData m = built;
    rows[i].change(m);
    const cc::ClusterFailure failure = build_clusters(m);
    require(failure.error == rows[i].error && failure.draw == rows[i].draw, i, rows[i].note);
    require(m.clusters.empty() && m.levels.empty(), i, "both tables left empty");
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{Scenario{"split_and_boxes", &split_and_boxes}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
