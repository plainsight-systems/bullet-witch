#ifndef BW_RENDERING_PULLED_CUBE_FIXTURE_HPP
#define BW_RENDERING_PULLED_CUBE_FIXTURE_HPP

#include "bw/math/matrix/matrix4.hpp"
#include "bw/rendering/geometry/mesh_resources.hpp"
#include "bw/rendering/geometry/pulled_mesh_fixture.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/shaders/artifact.hpp"

#include <cstdint>
#include <string>

// Responsibility: the fixture draw of the generated cube
// (bw/rendering/geometry/pulled_mesh_fixture.hpp): its group on the GPU,
// its pipeline and its one draw. It is what a host draws when it is given
// no content; it leaves with the fixture shaders, and
// bw/rendering/geometry/mesh_resources.hpp, which uploads it, stays.
//
// Contract.
//   1. create_pulled_cube makes the cube (make_fixture_cube), checks that
//      its layout validates and that its vertex_count vertices from base
//      vertex 0 lie within its streams (validate_vertex_stream_layout,
//      validate_vertex_range; CubeInvalid otherwise), uploads it with
//      create_group_resources under the name "cube" (GroupFailed), and
//      makes the states and the pipeline: kPulledMeshShader and
//      kVertexColorShader, clockwise faces culled, no depth target, depth
//      neither tested nor written, one color target of `color_format`, one
//      sample, triangle lists, precompiled before it is created
//      (StateFailed, PipelineFailed).
//   2. record_pulled_cube sets the pipeline, sets a PulledMeshDraw block
//      (world_to_clip and the group's block), binds the index buffer and
//      records one draw_indexed of index_count indices.
//      Preconditions: `list` is inside a render pass with one color target
//      of the format in 1 and no depth target, with the viewport and
//      scissor set (begin_cleared_pass with a null depth does both).
//   3. On a failure `out` is left empty. The detail text carries the
//      failing step's error and detail; its wording is not part of the
//      contract.
//
// Read guidance: I.1, E.27, R.1.
namespace bw::rendering {

struct PulledCube {
  geometry::MeshGroupResources group{};
  std::uint32_t index_count{};
  rhi::Ref<rhi::RasterizerState> rasterizer{};
  rhi::Ref<rhi::DepthStencilState> depth_stencil{};
  rhi::Ref<rhi::BlendState> blend{};
  rhi::Ref<rhi::GraphicsPipeline> pipeline{};
};

enum class PulledCubeError : std::uint8_t {
  None,
  CubeInvalid,
  GroupFailed,
  StateFailed,
  PipelineFailed,
};

struct PulledCubeFailure {
  PulledCubeError error{PulledCubeError::None};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == PulledCubeError::None; }
};

// `vertex` and `pixel` are the loaded artifacts of kPulledMeshShader and
// kVertexColorShader; both must outlive the call.
[[nodiscard]] PulledCubeFailure create_pulled_cube(rhi::Device& device,
                                                   const shaders::Artifact& vertex,
                                                   const shaders::Artifact& pixel,
                                                   rhi::Format color_format, PulledCube& out);

void record_pulled_cube(rhi::CommandList& list, const PulledCube& cube,
                        const math::Matrix4f& world_to_clip);

} // namespace bw::rendering

#endif
