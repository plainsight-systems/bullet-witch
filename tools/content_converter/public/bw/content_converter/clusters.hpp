#ifndef BW_CONTENT_CONVERTER_CLUSTERS_HPP
#define BW_CONTENT_CONVERTER_CLUSTERS_HPP
#include "bw/content_converter/asset_data.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"

#include <cstdint>

// Responsibility: the cluster and level tables of a converted mesh
// (docs/architecture/content.md, the Mesh row; mesh_asset.hpp, rules 10
// and 11): each draw's index range split into clusters, each with the box
// of its vertices and, when the draw is bound whole to one bone, that bone;
// one detail level over all of them. A pure function of the mesh data; the
// encoder writes what it fills.
// A cluster is a fixed run of a mesh's triangles with its own bounds. The
// runs follow the authored index order; a builder that reorders triangles
// spatially is taken when a measured cull rate on a stage shows the runs'
// boxes are the limit. No detail level is generated: no input mesh
// carries a second level, so every mesh has one.
//
// Contract.
//   1. build_clusters(mesh) replaces mesh.clusters and mesh.levels. For
//      each draw in draw order, its index range [first_index, first_index
//      + index_count) becomes ceil(triangles / kClusterTriangles)
//      consecutive clusters in index order, every one kClusterTriangles
//      triangles but the last, which holds the remainder; together they
//      cover the range once.
//   2. A cluster's box is the minimum and maximum, per axis, of the
//      positions of the vertices its indices name: vertex index +
//      base_vertex of the draw, read from the Position attribute of the
//      draw's group.
//   3. A cluster's bone: when the draw has a bone map and its group
//      carries BoneSlots and BoneWeights, and every vertex the draw's
//      indices name has its whole weight (255) on one slot and that slot is
//      the same for all of them, the bone is the draw's bone map entry for
//      that slot, the same in every cluster of the draw; otherwise
//      kNoClusterBone.
//   4. mesh.levels is one level: every cluster, screen size
//      kSingleLevelScreenSize. A mesh with no draws gets one level of no
//      clusters, which the encoder refuses (mesh_asset.hpp, rule 11,
//      LevelRangeInvalid): a mesh that draws nothing is not converted.
//   5. Failures, each naming the draw and leaving both tables empty:
//      PositionMissing (the draw's group has no Position attribute of type
//      Float3); DrawRangeInvalid (no group holds the draw, its index
//      range leaves its group's indices, an index + base_vertex is at or
//      past the group's vertex count, or a named vertex's bytes leave its
//      stream); SlotOutOfRange (the one slot of contract 3 is at or past
//      the bone map's entry count).
//
// Core guidelines read: I.1 (a pure function of the mesh data), E.27, F.20
// (the failure is the return value). Performance guidelines read: CDSA.32
// (the tables are made once, in the consumer's layout).
namespace bw::content_converter {

inline constexpr std::uint32_t kClusterTriangles = geometry::kMaxClusterIndices / 3;
inline constexpr float kSingleLevelScreenSize = 1.0F;

enum class ClusterError : std::uint8_t {
  None,
  PositionMissing,
  DrawRangeInvalid,
  SlotOutOfRange,
};

struct ClusterFailure {
  ClusterError error{ClusterError::None};
  std::uint32_t draw{};
  [[nodiscard]] bool ok() const noexcept { return error == ClusterError::None; }
};

[[nodiscard]] ClusterFailure build_clusters(MeshAssetData& mesh);

} // namespace bw::content_converter

#endif
