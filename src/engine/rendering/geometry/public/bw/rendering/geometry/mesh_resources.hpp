#ifndef BW_RENDERING_GEOMETRY_MESH_RESOURCES_HPP
#define BW_RENDERING_GEOMETRY_MESH_RESOURCES_HPP

#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: mesh bytes as GPU resources: the buffers, views and draw
// ranges every draw of a mesh needs, whatever shades it. One function
// uploads one group from a layout and its bytes; one makes a whole decoded
// payload from groups uploaded that way. The model loader and the cube
// fixture take their meshes from here.
//
// Contract.
//   1. create_group_resources: stream s of the group is a buffer of
//      streams[s] as given (usage ShaderResource, initial access
//      ShaderResourceNonPixel) with a raw view; indices is a buffer of the
//      16-bit index bytes (usage Index, stride 2, initial access
//      VertexOrIndexBuffer); block is make_vertex_streams(layout, the
//      views' buffer indices and vertex_count, 0): base vertex 0, which a
//      draw replaces with its own. Resources are named "<debug_name> stream
//      <s>" and "<debug_name> indices". A group has no draws, so no draw
//      range is checked here: a caller that draws a group made alone
//      checks its range (validate_vertex_range). Preconditions: the layout
//      validates (validate_vertex_stream_layout) and streams.size() equals
//      layout.stream_count.
//   2. create_mesh_resources: groups holds one entry per group of the
//      payload, in group order, each made as in 1 from the payload's bytes
//      under the name "<debug_name> group <g>".
//   3. draws holds every draw of the payload in payload order, each with
//      the index of the group whose draw range holds it. material is the
//      payload's material index or kNoMaterial; bone_map is the payload's.
//   4. Before a draw is kept, validate_vertex_range checks it against its
//      group's layout and stream sizes; a draw that fails is
//      DrawRangeInvalid with the draw's index and the range error. The
//      range checked is the one the draw declares; the index values are
//      uploaded unread (mesh_asset.hpp, rule 6), so an index outside its
//      draw's declared range is not caught here.
//   5. center and radius are the payload's bounding sphere and
//      material_count its material count, unchanged.
//   6. On any failure of either function `out` is left empty: no group, no
//      draw, no buffer kept. The failure names the step, the group or draw
//      index, the device's error and the device's detail text.
//   Precondition of create_mesh_resources: `mesh` came from a
//   decode_mesh_asset that returned None. Neither function keeps the bytes
//   it is given: the buffers hold copies.
//
// Read guidance: R.1 (the Refs own every resource), I.1, E.27, F.20 (the
// failure is the return value); CDSA.32 (bytes are uploaded as stored, with
// no CPU transform).
namespace bw::geometry {

struct MeshGroupResources {
  VertexStreamLayout layout{};
  std::uint32_t vertex_count{};
  std::uint32_t stream_count{};
  std::array<rhi::Ref<rhi::Buffer>, kMaxVertexStreams> streams{};
  std::array<rhi::Ref<rhi::ShaderResourceView>, kMaxVertexStreams> views{};
  rhi::Ref<rhi::Buffer> indices{};
  VertexStreams block{};
};

struct MeshDrawRange {
  std::uint32_t group{};
  std::uint32_t base_vertex{};
  std::uint32_t first_index{};
  std::uint32_t index_count{};
  std::uint32_t material{kNoMaterial};
  std::uint32_t bone_map{};
};

struct MeshResources {
  std::vector<MeshGroupResources> groups{};
  std::vector<MeshDrawRange> draws{};
  std::array<float, 3> center{};
  float radius{};
  std::uint32_t material_count{};
};

enum class MeshResourceError : std::uint8_t {
  None,
  DrawRangeInvalid,
  StreamBufferFailed,
  StreamViewFailed,
  IndexBufferFailed,
};

struct MeshResourceFailure {
  MeshResourceError error{MeshResourceError::None};
  // The draw for DrawRangeInvalid, otherwise the group (0 from
  // create_group_resources).
  std::uint32_t index{};
  // Set for DrawRangeInvalid.
  VertexStreamError range{VertexStreamError::None};
  // Set for the three device failures, with the device's detail text.
  rhi::ResourceError resource{rhi::ResourceError::None};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == MeshResourceError::None; }
};

[[nodiscard]] MeshResourceFailure create_group_resources(
    rhi::Device& device, const VertexStreamLayout& layout, std::uint32_t vertex_count,
    std::span<const std::span<const std::byte>> streams, std::span<const std::byte> indices,
    std::string_view debug_name, MeshGroupResources& out);

[[nodiscard]] MeshResourceFailure create_mesh_resources(rhi::Device& device,
                                                        const MeshAssetView& mesh,
                                                        std::string_view debug_name,
                                                        MeshResources& out);

} // namespace bw::geometry

#endif
