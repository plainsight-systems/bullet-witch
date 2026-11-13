#include "bw/rendering/scene/scene_geometry.hpp"

#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/cluster_records.hpp"
#include "bw/rendering/scene/instance.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Responsibility: the out-of-line part of
// bw/rendering/scene/scene_geometry.hpp.
//
// Readings of the header that the code encodes:
//   Room is checked for clusters, then levels, then indices, before any
//   table is touched; the first without room is the one named.
//   An empty level run starts where the draw's clusters at that level
//   would: after the level's clusters of every earlier draw.
//   A draw with no level-0 cluster has an empty box (min above max).
//   clear() also forgets what was uploaded, so the next upload copies the
//   new tables from their start.
//   upload_geometry locks each table's range appended since the last
//   upload.
//   The buffers are created together and kept only when all three are.
namespace bw::scene {

namespace {

std::uint32_t u32(std::size_t value) noexcept { return static_cast<std::uint32_t>(value); }

ClusterRecord cluster_record(const geometry::MeshClusterRecord& c,
                             std::uint32_t index_base) noexcept {
  ClusterRecord record{};
  record.first_index = c.first_index + index_base;
  record.index_count = c.index_count;
  record.bone = c.bone;
  record.center =
      shaders::Float3{(c.box_min[0] + c.box_max[0]) * 0.5F, (c.box_min[1] + c.box_max[1]) * 0.5F,
                      (c.box_min[2] + c.box_max[2]) * 0.5F};
  record.extent =
      shaders::Float3{(c.box_max[0] - c.box_min[0]) * 0.5F, (c.box_max[1] - c.box_min[1]) * 0.5F,
                      (c.box_max[2] - c.box_min[2]) * 0.5F};
  return record;
}

// The device's refusal of a lock or unlock, or None.
[[nodiscard]] GeometryUploadFailure write_range(rhi::Device& device, rhi::Buffer& buffer,
                                                std::uint64_t offset,
                                                std::span<const std::byte> bytes) {
  if (bytes.empty()) {
    return {};
  }
  const rhi::LockResult locked = device.lock_buffer(buffer, rhi::BufferRange{offset, bytes.size()});
  if (!locked.ok()) {
    return {rhi::ResourceError::None, locked.error(),
            "lock_buffer refused a scene geometry table: " + std::string(locked.detail())};
  }
  std::memcpy(locked.value().data(), bytes.data(), bytes.size());
  const rhi::LockError unlocked = device.unlock_buffer(buffer);
  if (unlocked != rhi::LockError::None) {
    // unlock_buffer gives no detail text; the buffer's name stands for it.
    return {rhi::ResourceError::None, unlocked,
            "unlock_buffer refused a scene geometry table: " + std::string(buffer.debug_name())};
  }
  return {};
}

} // namespace

SceneGeometry::SceneGeometry(const SceneGeometryCapacity& capacity) : capacity_(capacity) {
  clusters_.reserve(capacity.clusters);
  levels_.reserve(capacity.levels);
  indices_.reserve(capacity.indices);
}

// One indirect draw per pipeline needs that pipeline's geometry in one
// pool, so every mesh's indices go into the one index buffer under the
// multi-draw. Vertices are not pooled: they are pulled by descriptor index
// from each group's own streams.
GeometryFailure SceneGeometry::add_mesh(const geometry::MeshAssetView& mesh,
                                        std::span<const geometry::VertexStreams> group_blocks,
                                        SceneMesh& out) {
  const std::uint32_t draws = mesh.draw_count();
  const std::uint32_t level_count = mesh.level_count();
  std::uint64_t index_total = 0;
  for (std::uint32_t g = 0; g < mesh.group_count(); ++g) {
    index_total += mesh.group(g).index_count;
  }
  if (std::uint64_t{clusters_.size()} + mesh.cluster_count() > capacity_.clusters) {
    return {GeometryError::Full, GeometryTable::Clusters};
  }
  if (std::uint64_t{levels_.size()} + (std::uint64_t{draws} * level_count) > capacity_.levels) {
    return {GeometryError::Full, GeometryTable::Levels};
  }
  if (std::uint64_t{indices_.size()} + index_total > capacity_.indices) {
    return {GeometryError::Full, GeometryTable::Indices};
  }

  // Each group's place in the index buffer, and each draw's group.
  std::vector<std::uint32_t> group_base(mesh.group_count());
  std::vector<std::uint32_t> draw_group(draws);
  for (std::uint32_t g = 0; g < mesh.group_count(); ++g) {
    const geometry::MeshGroupRecord group = mesh.group(g);
    group_base[g] = u32(indices_.size());
    const std::span<const std::byte> bytes = mesh.index_bytes(group);
    for (std::size_t i = 0; i + 1U < bytes.size(); i += 2U) {
      indices_.push_back(static_cast<std::uint16_t>(
          std::to_integer<unsigned>(bytes[i]) | (std::to_integer<unsigned>(bytes[i + 1U]) << 8U)));
    }
    for (std::uint32_t d = group.first_draw; d < group.first_draw + group.draw_count; ++d) {
      draw_group[d] = g;
    }
  }

  const std::uint32_t cluster_base = u32(clusters_.size());
  for (std::uint32_t c = 0; c < mesh.cluster_count(); ++c) {
    const geometry::MeshClusterRecord cluster = mesh.cluster(c);
    clusters_.push_back(cluster_record(cluster, group_base[draw_group[cluster.draw]]));
  }

  out.draws.assign(draws, InstanceGeometry{});
  for (std::uint32_t d = 0; d < draws; ++d) {
    InstanceGeometry& geometry = out.draws[d];
    geometry.first_level = u32(levels_.size());
    geometry.level_count = level_count;
    geometry.streams = group_blocks[draw_group[d]];
    geometry.streams.base_vertex = mesh.draw(d).base_vertex;
    geometry.bounds.min = math::Vector3f{std::numeric_limits<float>::max()};
    geometry.bounds.max = math::Vector3f{std::numeric_limits<float>::lowest()};
    for (std::uint32_t l = 0; l < level_count; ++l) {
      const geometry::MeshLevelRecord level = mesh.level(l);
      // Rule 11: a level's clusters are ordered by draw, so the draw's are
      // the run between the first at or past d and the first past d.
      std::uint32_t first = level.first_cluster;
      const std::uint32_t end = level.first_cluster + level.cluster_count;
      while (first < end && mesh.cluster(first).draw < d) {
        ++first;
      }
      std::uint32_t last = first;
      while (last < end && mesh.cluster(last).draw == d) {
        if (l == 0U) {
          const geometry::MeshClusterRecord c = mesh.cluster(last);
          Box& b = geometry.bounds;
          b.min = math::Vector3f{std::min(b.min.x, c.box_min[0]), std::min(b.min.y, c.box_min[1]),
                                 std::min(b.min.z, c.box_min[2])};
          b.max = math::Vector3f{std::max(b.max.x, c.box_max[0]), std::max(b.max.y, c.box_max[1]),
                                 std::max(b.max.z, c.box_max[2])};
        }
        ++last;
      }
      levels_.push_back(LevelRecord{cluster_base + first, last - first, level.screen_size, 0});
      geometry.cluster_count += last - first;
    }
  }
  return {};
}

void SceneGeometry::clear() noexcept {
  clusters_.clear();
  levels_.clear();
  indices_.clear();
  uploaded_ = SceneGeometryCapacity{};
}

// The friend the header names: the upload reads and marks the tables.
struct SceneGeometryUpload {
  static GeometryUploadFailure run(rhi::Device& device, SceneGeometry& geometry,
                                   GeometryBuffers& buffers) {
    if (!buffers.clusters) {
      GeometryBuffers made;
      if (GeometryUploadFailure f = create(device, geometry.capacity_, made); !f.ok()) {
        return f;
      }
      buffers = std::move(made);
    }
    const SceneGeometryCapacity& done = geometry.uploaded_;
    const std::span<const ClusterRecord> clusters =
        std::span<const ClusterRecord>(geometry.clusters_).subspan(done.clusters);
    const std::span<const LevelRecord> levels =
        std::span<const LevelRecord>(geometry.levels_).subspan(done.levels);
    const std::span<const std::uint16_t> indices =
        std::span<const std::uint16_t>(geometry.indices_).subspan(done.indices);
    if (GeometryUploadFailure f = write_range(device, *buffers.clusters,
                                              std::uint64_t{done.clusters} * kClusterRecordBytes,
                                              std::as_bytes(clusters));
        !f.ok()) {
      return f;
    }
    if (GeometryUploadFailure f =
            write_range(device, *buffers.levels, std::uint64_t{done.levels} * kLevelRecordBytes,
                        std::as_bytes(levels));
        !f.ok()) {
      return f;
    }
    if (GeometryUploadFailure f = write_range(device, *buffers.indices,
                                              std::uint64_t{done.indices} * sizeof(std::uint16_t),
                                              std::as_bytes(indices));
        !f.ok()) {
      return f;
    }
    geometry.uploaded_ =
        SceneGeometryCapacity{u32(geometry.clusters_.size()), u32(geometry.levels_.size()),
                              u32(geometry.indices_.size())};
    return {};
  }

private:
  static GeometryUploadFailure create(rhi::Device& device, const SceneGeometryCapacity& capacity,
                                      GeometryBuffers& out) {
    rhi::BufferDescription table{};
    table.usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::Upload;
    table.initial_access = rhi::kShaderResourceMask;
    table.size = std::uint64_t{capacity.clusters} * kClusterRecordBytes;
    table.debug_name = "scene cluster table";
    rhi::BufferResult clusters = device.create_buffer(table);
    if (!clusters.ok()) {
      return {clusters.error(), rhi::LockError::None, std::string(clusters.detail())};
    }
    table.size = std::uint64_t{capacity.levels} * kLevelRecordBytes;
    table.debug_name = "scene level table";
    rhi::BufferResult levels = device.create_buffer(table);
    if (!levels.ok()) {
      return {levels.error(), rhi::LockError::None, std::string(levels.detail())};
    }
    rhi::BufferDescription index{};
    index.size = std::uint64_t{capacity.indices} * sizeof(std::uint16_t);
    index.stride = sizeof(std::uint16_t);
    index.usage = rhi::BufferUsage::Index | rhi::BufferUsage::Upload;
    index.initial_access = rhi::Access::VertexOrIndexBuffer;
    index.debug_name = "scene index buffer";
    rhi::BufferResult indices = device.create_buffer(index);
    if (!indices.ok()) {
      return {indices.error(), rhi::LockError::None, std::string(indices.detail())};
    }
    rhi::ShaderResourceViewResult clusters_view =
        device.create_shader_resource_view(clusters.value(), rhi::BufferViewDescription{});
    if (!clusters_view.ok()) {
      return {clusters_view.error(), rhi::LockError::None, std::string(clusters_view.detail())};
    }
    rhi::ShaderResourceViewResult levels_view =
        device.create_shader_resource_view(levels.value(), rhi::BufferViewDescription{});
    if (!levels_view.ok()) {
      return {levels_view.error(), rhi::LockError::None, std::string(levels_view.detail())};
    }
    out.clusters = std::move(clusters.value());
    out.clusters_view = std::move(clusters_view.value());
    out.levels = std::move(levels.value());
    out.levels_view = std::move(levels_view.value());
    out.indices = std::move(indices.value());
    return {};
  }
};

// The same index buffer serves the depth and opaque passes.
GeometryUploadFailure upload_geometry(rhi::Device& device, SceneGeometry& geometry,
                                      GeometryBuffers& buffers) {
  return SceneGeometryUpload::run(device, geometry, buffers);
}

} // namespace bw::scene
