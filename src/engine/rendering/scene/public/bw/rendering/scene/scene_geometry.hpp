#ifndef BW_RENDERING_SCENE_SCENE_GEOMETRY_HPP
#define BW_RENDERING_SCENE_SCENE_GEOMETRY_HPP

#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/cluster_records.hpp"
#include "bw/rendering/scene/instance.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Responsibility: the mesh tables the scene keeps for culling and for the
// counted multi-draw (docs/architecture/scene.md, "The instance", the Mesh
// row: cluster table and detail levels reach culling through the scene on
// entry): every registered mesh's clusters in one cluster table, its
// detail levels per draw in one level table (cluster_records.hpp), and
// its indices in one index buffer. One index buffer, because a counted multi-draw draws every
// record of a list with the index buffer bound when it is issued, and a list holds clusters of many
// meshes; the same index buffer serves the depth and opaque passes. Vertices are not pooled: they
// are pulled by descriptor index from each group's own streams. One mesh is never removed alone: a
// stage's meshes arrive and leave together, so clear() empties the tables whole.
//
// Contract.
//   1. A SceneGeometry is created with capacities in clusters, levels and
//      indices and never grows.
//   2. add_mesh(mesh, group_blocks, out) appends one mesh: its clusters in
//      payload order, each with first_index moved to where its group's
//      indices sit in the index buffer and its box as centre and extent;
//      its groups' indices in group order; and for each draw d and each
//      level l of the mesh, in draw-major order, one level record: the run
//      of level l's clusters whose draw is d (mesh_asset.hpp, rule 11, makes
//      it one run; empty when the level holds none) and the level's screen
//      size. out.draws[d] is the draw's InstanceGeometry: first_level its
//      first level record, level_count the mesh's level count,
//      cluster_count its clusters over every level, streams
//      group_blocks[g] of its group with base_vertex the draw's, bounds the
//      union of its level-0 clusters' boxes. A table without room is Full,
//      names the table, and appends nothing. Preconditions: `mesh` decoded
//      (decode_mesh_asset returned None); group_blocks holds one block per
//      group, in group order.
//   3. clusters(), levels() and indices() are the tables as appended.
//      clear() empties them; every InstanceGeometry given before is then
//      meaningless.
//   4. upload_geometry copies what was appended since the last upload into
//      the three buffers, created on the first call at the capacities:
//      clusters and levels byte addressed with raw views, indices a 16-bit
//      index buffer. A device refusal is returned by its own name (the
//      buffer's or view's ResourceError, or the LockError of a refused
//      lock or unlock) with the device's detail text, or the buffer's
//      name for a refused unlock, which has none, and nothing is marked
//      uploaded.
//
// Core guidelines read: I.1, E.27, R.1, F.20. Performance guidelines read:
// GPU.9 (one buffer per table, ranges inside it), CDSA.26 (a mesh's tables
// are named by index), MEM.9 (made at load, nothing per frame).
namespace bw::scene {

struct SceneGeometryCapacity {
  std::uint32_t clusters{};
  std::uint32_t levels{};
  std::uint32_t indices{};
};

enum class GeometryError : std::uint8_t {
  None,
  Full,
};
enum class GeometryTable : std::uint8_t { Clusters, Levels, Indices };

struct GeometryFailure {
  GeometryError error{GeometryError::None};
  GeometryTable table{GeometryTable::Clusters};
  [[nodiscard]] bool ok() const noexcept { return error == GeometryError::None; }
};

// One entry per draw of the mesh, in draw order.
struct SceneMesh {
  std::vector<InstanceGeometry> draws{};
};

class SceneGeometry {
public:
  explicit SceneGeometry(const SceneGeometryCapacity& capacity);

  [[nodiscard]] GeometryFailure add_mesh(const geometry::MeshAssetView& mesh,
                                         std::span<const geometry::VertexStreams> group_blocks,
                                         SceneMesh& out);
  void clear() noexcept;

  [[nodiscard]] const SceneGeometryCapacity& capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::span<const ClusterRecord> clusters() const noexcept { return clusters_; }
  [[nodiscard]] std::span<const LevelRecord> levels() const noexcept { return levels_; }
  [[nodiscard]] std::span<const std::uint16_t> indices() const noexcept { return indices_; }

private:
  friend struct SceneGeometryUpload;
  SceneGeometryCapacity capacity_{};
  std::vector<ClusterRecord> clusters_{};
  std::vector<LevelRecord> levels_{};
  std::vector<std::uint16_t> indices_{};
  // What upload_geometry has copied of each table.
  SceneGeometryCapacity uploaded_{};
};

struct GeometryBuffers {
  rhi::Ref<rhi::Buffer> clusters{};
  rhi::Ref<rhi::ShaderResourceView> clusters_view{};
  rhi::Ref<rhi::Buffer> levels{};
  rhi::Ref<rhi::ShaderResourceView> levels_view{};
  rhi::Ref<rhi::Buffer> indices{};
};

// `resource` names a refused buffer or view, `lock` a refused lock or
// unlock of one of the three buffers; at most one is set.
struct GeometryUploadFailure {
  rhi::ResourceError resource{rhi::ResourceError::None};
  rhi::LockError lock{rhi::LockError::None};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept {
    return resource == rhi::ResourceError::None && lock == rhi::LockError::None;
  }
};

[[nodiscard]] GeometryUploadFailure upload_geometry(rhi::Device& device, SceneGeometry& geometry,
                                                    GeometryBuffers& buffers);

} // namespace bw::scene

#endif
