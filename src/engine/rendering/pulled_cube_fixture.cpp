#include "bw/rendering/pulled_cube_fixture.hpp"

#include "bw/math/matrix/matrix4.hpp"
#include "bw/rendering/geometry/mesh_resources.hpp"
#include "bw/rendering/geometry/pulled_mesh_fixture.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/shaders/artifact.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

// Responsibility: the out-of-line part of
// bw/rendering/pulled_cube_fixture.hpp.
namespace bw::rendering {

namespace {

template <class Code>
PulledCubeFailure failed(PulledCubeError error, std::string_view what, Code code,
                         std::string_view detail) {
  std::string text(what);
  text += ": error " + std::to_string(static_cast<unsigned>(code));
  if (!detail.empty()) {
    text += ": ";
    text += detail;
  }
  return PulledCubeFailure{error, std::move(text)};
}

// The cube's bytes on the GPU, its range checked once.
PulledCubeFailure upload_cube(rhi::Device& device, PulledCube& built) {
  const geometry::FixtureCube cube = geometry::make_fixture_cube();
  const geometry::VertexStreamError layout = geometry::validate_vertex_stream_layout(cube.layout);
  if (layout != geometry::VertexStreamError::None) {
    return failed(PulledCubeError::CubeInvalid, "cube layout", layout, {});
  }
  const std::array<std::uint64_t, 2> stream_bytes{cube.stream0.size(), cube.stream1.size()};
  const geometry::VertexStreamError range =
      geometry::validate_vertex_range(cube.layout, stream_bytes, 0, cube.vertex_count);
  if (range != geometry::VertexStreamError::None) {
    return failed(PulledCubeError::CubeInvalid, "cube vertex range", range, {});
  }
  const std::array<std::span<const std::byte>, 2> streams{cube.stream0, cube.stream1};
  const geometry::MeshResourceFailure uploaded =
      geometry::create_group_resources(device, cube.layout, cube.vertex_count, streams,
                                       std::as_bytes(std::span(cube.indices)), "cube", built.group);
  if (!uploaded.ok()) {
    return failed(PulledCubeError::GroupFailed,
                  "cube upload step " + std::to_string(static_cast<unsigned>(uploaded.error)),
                  uploaded.resource, uploaded.detail);
  }
  built.index_count = cube.index_count;
  return {};
}

PulledCubeFailure create_states(rhi::Device& device, PulledCube& built) {
  rhi::RasterizerDescription cull_back{};
  cull_back.cull = rhi::CullMode::Clockwise;
  rhi::RasterizerStateResult rasterizer = device.rasterizer_state(cull_back);
  if (!rasterizer.ok()) {
    return failed(PulledCubeError::StateFailed, "rasterizer state", rasterizer.error(),
                  rasterizer.detail());
  }
  built.rasterizer = std::move(rasterizer.value());
  rhi::DepthStencilDescription depth_off{};
  depth_off.depth_write = false;
  depth_off.depth_test = rhi::CompareFunction::Always;
  rhi::DepthStencilStateResult depth_stencil = device.depth_stencil_state(depth_off);
  if (!depth_stencil.ok()) {
    return failed(PulledCubeError::StateFailed, "depth stencil state", depth_stencil.error(),
                  depth_stencil.detail());
  }
  built.depth_stencil = std::move(depth_stencil.value());
  rhi::BlendStateResult blend = device.blend_state(rhi::BlendDescription{});
  if (!blend.ok()) {
    return failed(PulledCubeError::StateFailed, "blend state", blend.error(), blend.detail());
  }
  built.blend = std::move(blend.value());
  return {};
}

} // namespace

PulledCubeFailure create_pulled_cube(rhi::Device& device,
                                     // The two artifacts in pipeline order.
                                     // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                                     const shaders::Artifact& vertex,
                                     const shaders::Artifact& pixel, rhi::Format color_format,
                                     PulledCube& out) {
  out = PulledCube{};
  PulledCube built;
  PulledCubeFailure failure = upload_cube(device, built);
  if (!failure.ok()) {
    return failure;
  }
  failure = create_states(device, built);
  if (!failure.ok()) {
    return failure;
  }
  rhi::GraphicsPipelineDescription description{};
  description.vertex_shader = &vertex;
  description.pixel_shader = &pixel;
  description.rasterizer = &*built.rasterizer;
  description.depth_stencil = &*built.depth_stencil;
  description.blend = &*built.blend;
  description.targets.color_formats[0] = color_format;
  description.targets.color_count = 1;
  description.targets.sample_count = 1;
  description.primitive = rhi::PrimitiveType::TriangleList;
  description.debug_name = "pulled cube fixture";
  const rhi::Status<rhi::PipelineError> precompiled =
      device.precompile(std::span<const rhi::GraphicsPipelineDescription>{&description, 1}, {});
  if (!precompiled.ok()) {
    return failed(PulledCubeError::PipelineFailed, "pipeline precompile", precompiled.error(),
                  precompiled.detail());
  }
  rhi::GraphicsPipelineResult pipeline = device.graphics_pipeline(description);
  if (!pipeline.ok()) {
    return failed(PulledCubeError::PipelineFailed, "graphics pipeline", pipeline.error(),
                  pipeline.detail());
  }
  built.pipeline = std::move(pipeline.value());
  out = std::move(built);
  return {};
}

void record_pulled_cube(rhi::CommandList& list, const PulledCube& cube,
                        const math::Matrix4f& world_to_clip) {
  geometry::PulledMeshDraw block{};
  block.world_to_clip = scene::shader_matrix(world_to_clip);
  block.streams = cube.group.block;
  list.set_graphics_pipeline(*cube.pipeline);
  list.set_draw_block(block);
  list.set_index_buffer(*cube.group.indices, rhi::IndexFormat::Uint16, 0);
  list.draw_indexed(rhi::DrawIndexedArguments{cube.index_count, 1, 0, 0, 0});
}

} // namespace bw::rendering
