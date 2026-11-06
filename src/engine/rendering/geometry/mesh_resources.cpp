#include "bw/rendering/geometry/mesh_resources.hpp"

#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

// Responsibility: the out-of-line part of
// bw/rendering/geometry/mesh_resources.hpp: one group's streams and indices
// uploaded as given, and a payload's groups made that way with their draws
// range-checked.
namespace bw::geometry {

namespace {

MeshResourceFailure device_failure(MeshResourceError error, rhi::ResourceError resource,
                                   std::string_view detail) {
  MeshResourceFailure failure{};
  failure.error = error;
  failure.resource = resource;
  failure.detail = std::string(detail);
  return failure;
}

} // namespace

MeshResourceFailure create_group_resources(rhi::Device& device, const VertexStreamLayout& layout,
                                           std::uint32_t vertex_count,
                                           std::span<const std::span<const std::byte>> streams,
                                           std::span<const std::byte> indices,
                                           std::string_view debug_name, MeshGroupResources& out) {
  out = MeshGroupResources{};
  MeshGroupResources group;
  group.layout = layout;
  group.vertex_count = vertex_count;
  group.stream_count = layout.stream_count;
  std::array<VertexStreamBinding, kMaxVertexStreams> bindings{};
  for (std::uint32_t s = 0; s < group.stream_count; ++s) {
    const std::string name = std::string(debug_name) + " stream " + std::to_string(s);
    rhi::BufferDescription description{};
    description.size = streams[s].size();
    description.usage = rhi::BufferUsage::ShaderResource;
    description.initial_access = rhi::Access::ShaderResourceNonPixel;
    description.debug_name = name;
    rhi::BufferResult created = device.create_buffer(description, streams[s]);
    if (!created.ok()) {
      return device_failure(MeshResourceError::StreamBufferFailed, created.error(),
                            created.detail());
    }
    group.streams[s] = std::move(created.value());
    rhi::ShaderResourceViewResult viewed =
        device.create_shader_resource_view(group.streams[s], rhi::BufferViewDescription{});
    if (!viewed.ok()) {
      return device_failure(MeshResourceError::StreamViewFailed, viewed.error(), viewed.detail());
    }
    group.views[s] = std::move(viewed.value());
    bindings[s] = VertexStreamBinding{group.views[s]->buffer_index(), vertex_count};
  }
  const std::string index_name = std::string(debug_name) + " indices";
  rhi::BufferDescription index_description{};
  index_description.size = indices.size();
  index_description.stride = sizeof(std::uint16_t);
  index_description.usage = rhi::BufferUsage::Index;
  index_description.initial_access = rhi::Access::VertexOrIndexBuffer;
  index_description.debug_name = index_name;
  rhi::BufferResult index_buffer = device.create_buffer(index_description, indices);
  if (!index_buffer.ok()) {
    return device_failure(MeshResourceError::IndexBufferFailed, index_buffer.error(),
                          index_buffer.detail());
  }
  group.indices = std::move(index_buffer.value());
  group.block = make_vertex_streams(layout, std::span(bindings).first(group.stream_count), 0);
  out = std::move(group);
  return {};
}

MeshResourceFailure create_mesh_resources(rhi::Device& device, const MeshAssetView& mesh,
                                          std::string_view debug_name, MeshResources& out) {
  out = MeshResources{};
  MeshResources built;
  built.groups.resize(mesh.group_count());
  built.draws.resize(mesh.draw_count());
  for (std::uint32_t g = 0; g < mesh.group_count(); ++g) {
    const MeshGroupRecord record = mesh.group(g);
    const VertexStreamLayout layout = mesh.layout(record);
    std::array<std::span<const std::byte>, kMaxVertexStreams> streams{};
    std::array<std::uint64_t, kMaxVertexStreams> stream_bytes{};
    for (std::uint32_t s = 0; s < record.stream_count; ++s) {
      streams[s] = mesh.stream_bytes(record, s);
      stream_bytes[s] = streams[s].size();
    }
    MeshResourceFailure failure = create_group_resources(
        device, layout, record.vertex_count, std::span(streams).first(record.stream_count),
        mesh.index_bytes(record), std::string(debug_name) + " group " + std::to_string(g),
        built.groups[g]);
    if (!failure.ok()) {
      failure.index = g;
      return failure;
    }
    for (std::uint32_t d = record.first_draw; d < record.first_draw + record.draw_count; ++d) {
      const MeshDrawRecord draw = mesh.draw(d);
      // The shader reads vertex SV_VertexID + base_vertex for indices from
      // min_index on, so the range checked runs to min_index + vertex_count.
      const VertexStreamError range =
          validate_vertex_range(layout, std::span(stream_bytes).first(record.stream_count),
                                draw.base_vertex, draw.min_index + draw.vertex_count);
      if (range != VertexStreamError::None) {
        MeshResourceFailure invalid{};
        invalid.error = MeshResourceError::DrawRangeInvalid;
        invalid.index = d;
        invalid.range = range;
        return invalid;
      }
      built.draws[d] = MeshDrawRange{
          g, draw.base_vertex, draw.first_index, draw.index_count, draw.material, draw.bone_map};
    }
  }
  built.center = mesh.header().center;
  built.radius = mesh.header().radius;
  built.material_count = mesh.header().material_count;
  out = std::move(built);
  return {};
}

} // namespace bw::geometry
