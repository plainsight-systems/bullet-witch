#include "bw/content_converter/clusters.hpp"

#include "bw/content_converter/asset_data.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bytes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

// Responsibility: the out-of-line part of bw/content_converter/clusters.hpp.
//
// Readings of the header that the code encodes:
//   A draw's group is the group whose draw range holds it. A draw no group
//   holds is DrawRangeInvalid before its Position attribute is looked for.
//   A draw whose index count is not a multiple of 3 is split the same way,
//   the last cluster taking what remains; the encoder names
//   DrawIndexRangeInvalid for it before it reads a cluster.
//   "Its whole weight on one slot" is one weight byte 255 and the other
//   three 0, read from BoneSlots of type UByte4 and BoneWeights of type
//   UByte4Norm; a group whose bone attributes have other types has no
//   rigid draw. A draw whose bone map is not in the mesh's table has no
//   bone; the encoder names BoneMapOutOfRange for it.
//   A draw's bone streams are those of a draw that can have a bone (a bone
//   map in the table, both attributes of those types); a named vertex whose
//   bytes leave one is DrawRangeInvalid, as for the Position stream.
namespace bw::content_converter {

namespace {

using geometry::VertexAttribute;
using geometry::VertexElementType;

constexpr std::uint32_t kFullWeight = 255;
constexpr std::uint32_t kBoneSlots = 4;

std::optional<geometry::VertexAttributeDescription> find_attribute(const MeshGroupData& group,
                                                                   VertexAttribute attribute) {
  for (std::uint8_t a = 0; a < group.layout.attribute_count; ++a) {
    if (group.layout.attributes[a].attribute == attribute) {
      return group.layout.attributes[a];
    }
  }
  return std::nullopt;
}

// One attribute of a group's vertices: the stream bytes, the stride and the
// attribute's offset in a vertex.
struct AttributeBytes {
  std::span<const std::byte> stream;
  std::uint32_t stride{};
  std::uint32_t offset{};
  std::uint32_t size{};

  // The byte position of `vertex`'s attribute, or no value when the stream
  // does not hold it.
  [[nodiscard]] std::optional<std::size_t> at(std::uint32_t vertex) const noexcept {
    const std::uint64_t position = (static_cast<std::uint64_t>(vertex) * stride) + offset;
    if (position + size > stream.size()) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(position);
  }
};

AttributeBytes bytes_of(const MeshGroupData& group, const geometry::VertexAttributeDescription& a) {
  const std::vector<std::byte>& stream = a.stream == 0U ? group.stream0 : group.stream1;
  return {stream, group.layout.streams[a.stream].stride, a.offset, geometry::element_bytes(a.type)};
}

const MeshGroupData* group_of(const MeshAssetData& mesh, std::uint32_t draw) noexcept {
  for (const MeshGroupData& group : mesh.groups) {
    if (draw >= group.first_draw && draw - group.first_draw < group.draw_count) {
      return &group;
    }
  }
  return nullptr;
}

struct BoneInputs {
  AttributeBytes slots;
  AttributeBytes weights;
};

std::optional<BoneInputs> bone_inputs(const MeshAssetData& mesh, const MeshGroupData& group,
                                      const geometry::MeshDrawRecord& draw) {
  if (draw.bone_map == geometry::kNoBoneMap || draw.bone_map >= mesh.bone_maps.size()) {
    return std::nullopt;
  }
  const auto slots = find_attribute(group, VertexAttribute::BoneSlots);
  const auto weights = find_attribute(group, VertexAttribute::BoneWeights);
  if (!slots || !weights || slots->type != VertexElementType::UByte4 ||
      weights->type != VertexElementType::UByte4Norm) {
    return std::nullopt;
  }
  return BoneInputs{bytes_of(group, *slots), bytes_of(group, *weights)};
}

// Where one vertex's slots and weights start, both inside their streams.
struct BoneBytes {
  std::size_t slots{};
  std::size_t weights{};
};

// The slot the vertex at `at` puts its whole weight on, or no value.
std::optional<std::uint32_t> whole_slot(const BoneInputs& in, BoneBytes at) {
  std::optional<std::uint32_t> slot;
  for (std::uint32_t k = 0; k < kBoneSlots; ++k) {
    const std::uint32_t weight = bytes::u8(in.weights.stream, at.weights + k);
    if (weight == kFullWeight && !slot) {
      slot = bytes::u8(in.slots.stream, at.slots + k);
    } else if (weight != 0U) {
      return std::nullopt;
    }
  }
  return slot;
}

ClusterFailure failed(ClusterError error, std::uint32_t draw, MeshAssetData& mesh) {
  mesh.clusters.clear();
  mesh.levels.clear();
  return {error, draw};
}

} // namespace

// A cluster is a fixed run of the mesh's triangles with its own bounds;
// the runs keep the authored index order, as clusters.hpp states.
ClusterFailure build_clusters(MeshAssetData& mesh) {
  mesh.clusters.clear();
  mesh.levels.clear();
  constexpr std::uint32_t kRunIndices = kClusterTriangles * 3U;
  for (std::uint32_t d = 0; d < mesh.draws.size(); ++d) {
    const MeshGroupData* group = group_of(mesh, d);
    if (group == nullptr) {
      return failed(ClusterError::DrawRangeInvalid, d, mesh);
    }
    const geometry::MeshDrawRecord& draw = mesh.draws[d];
    const auto position = find_attribute(*group, VertexAttribute::Position);
    if (!position || position->type != VertexElementType::Float3) {
      return failed(ClusterError::PositionMissing, d, mesh);
    }
    const AttributeBytes positions = bytes_of(*group, *position);
    if (static_cast<std::uint64_t>(draw.first_index) + draw.index_count > group->indices.size()) {
      return failed(ClusterError::DrawRangeInvalid, d, mesh);
    }
    const std::optional<BoneInputs> bones = bone_inputs(mesh, *group, draw);
    // The one slot every vertex of the draw is bound to, while it is one.
    std::optional<std::uint32_t> rigid_slot;
    bool rigid = bones.has_value();
    const std::size_t first_cluster = mesh.clusters.size();
    for (std::uint32_t run = 0; run < draw.index_count; run += kRunIndices) {
      geometry::MeshClusterRecord cluster{};
      cluster.first_index = draw.first_index + run;
      cluster.index_count = std::min(kRunIndices, draw.index_count - run);
      cluster.draw = d;
      cluster.box_min.fill(std::numeric_limits<float>::max());
      cluster.box_max.fill(std::numeric_limits<float>::lowest());
      for (std::uint32_t i = 0; i < cluster.index_count; ++i) {
        const std::uint64_t vertex =
            static_cast<std::uint64_t>(group->indices[cluster.first_index + i]) + draw.base_vertex;
        if (vertex >= group->vertex_count) {
          return failed(ClusterError::DrawRangeInvalid, d, mesh);
        }
        const auto v = static_cast<std::uint32_t>(vertex);
        const std::optional<std::size_t> at = positions.at(v);
        if (!at) {
          return failed(ClusterError::DrawRangeInvalid, d, mesh);
        }
        for (std::size_t axis = 0; axis < 3U; ++axis) {
          const float value = bytes::f32(positions.stream, *at + (axis * 4U));
          cluster.box_min[axis] = std::min(cluster.box_min[axis], value);
          cluster.box_max[axis] = std::max(cluster.box_max[axis], value);
        }
        // Every named vertex's bone bytes lie inside their streams, whether
        // or not the draw is still rigid.
        BoneBytes bone_bytes{};
        if (bones) {
          const std::optional<std::size_t> slots_at = bones->slots.at(v);
          const std::optional<std::size_t> weights_at = bones->weights.at(v);
          if (!slots_at || !weights_at) {
            return failed(ClusterError::DrawRangeInvalid, d, mesh);
          }
          bone_bytes = BoneBytes{*slots_at, *weights_at};
        }
        if (rigid) {
          const std::optional<std::uint32_t> slot = whole_slot(*bones, bone_bytes);
          rigid = slot.has_value() && (!rigid_slot || *rigid_slot == *slot);
          rigid_slot = slot;
        }
      }
      mesh.clusters.push_back(cluster);
    }
    if (rigid && rigid_slot) {
      const std::vector<std::uint32_t>& map = mesh.bone_maps[draw.bone_map];
      if (*rigid_slot >= map.size()) {
        return failed(ClusterError::SlotOutOfRange, d, mesh);
      }
      for (std::size_t c = first_cluster; c < mesh.clusters.size(); ++c) {
        mesh.clusters[c].bone = map[*rigid_slot];
      }
    }
  }
  mesh.levels.push_back(geometry::MeshLevelRecord{
      0, static_cast<std::uint32_t>(mesh.clusters.size()), kSingleLevelScreenSize, 0});
  return {};
}

} // namespace bw::content_converter
