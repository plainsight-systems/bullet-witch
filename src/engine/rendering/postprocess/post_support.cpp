#include "post_support.hpp"

#include "bw/rendering/postprocess/post_shaders.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"
#include "bw/rendering/shaders/permutation.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

// Responsibility: the out-of-line part of post_support.hpp.
namespace bw::postprocess::detail {

namespace {

[[nodiscard]] MakeFailure read(const std::filesystem::path& directory, std::string_view shader,
                               shaders::ArtifactFile& out) {
  const shaders::ArtifactFileFailure failure =
      shaders::read_artifact_file(directory, shader, shaders::PermutationId{0}, out);
  if (!failure.ok()) {
    return MakeFailure{MakeStep::Artifact, shaders::artifact_file_failure_text(failure, shader)};
  }
  return {};
}

} // namespace

MakeFailure make_compute_pipeline(rhi::Device& device,
                                  const std::filesystem::path& shader_directory,
                                  std::string_view shader, rhi::Ref<rhi::ComputePipeline>& out) {
  shaders::ArtifactFile artifact;
  MakeFailure failure = read(shader_directory, shader, artifact);
  if (!failure.ok()) {
    return failure;
  }
  rhi::ComputePipelineResult pipeline =
      device.compute_pipeline(rhi::ComputePipelineDescription{&artifact.artifact, shader});
  if (!pipeline.ok()) {
    return MakeFailure{MakeStep::Pipeline,
                       result_text(shader, pipeline.error(), pipeline.detail())};
  }
  out = std::move(pipeline.value());
  return {};
}

MakeFailure make_fullscreen_pipeline(rhi::Device& device,
                                     const std::filesystem::path& shader_directory,
                                     std::string_view pixel, rhi::Format format,
                                     std::string_view debug_name,
                                     rhi::Ref<rhi::GraphicsPipeline>& out) {
  shaders::ArtifactFile vertex_artifact;
  shaders::ArtifactFile pixel_artifact;
  MakeFailure failure = read(shader_directory, kFullscreenVertexShader, vertex_artifact);
  if (failure.ok()) {
    failure = read(shader_directory, pixel, pixel_artifact);
  }
  if (!failure.ok()) {
    return failure;
  }
  rhi::RasterizerStateResult rasterizer = device.rasterizer_state(rhi::RasterizerDescription{});
  if (!rasterizer.ok()) {
    return MakeFailure{MakeStep::State,
                       result_text("rasterizer state", rasterizer.error(), rasterizer.detail())};
  }
  rhi::DepthStencilDescription depth_off{};
  depth_off.depth_write = false;
  depth_off.depth_test = rhi::CompareFunction::Always;
  rhi::DepthStencilStateResult depth = device.depth_stencil_state(depth_off);
  if (!depth.ok()) {
    return MakeFailure{MakeStep::State,
                       result_text("depth stencil state", depth.error(), depth.detail())};
  }
  rhi::BlendStateResult blend = device.blend_state(rhi::BlendDescription{});
  if (!blend.ok()) {
    return MakeFailure{MakeStep::State, result_text("blend state", blend.error(), blend.detail())};
  }
  rhi::GraphicsPipelineDescription description{};
  description.vertex_shader = &vertex_artifact.artifact;
  description.pixel_shader = &pixel_artifact.artifact;
  description.rasterizer = rasterizer.value().get();
  description.depth_stencil = depth.value().get();
  description.blend = blend.value().get();
  description.targets.color_formats[0] = format;
  description.targets.color_count = 1;
  description.targets.sample_count = 1;
  description.primitive = rhi::PrimitiveType::TriangleList;
  description.debug_name = debug_name;
  rhi::GraphicsPipelineResult pipeline = device.graphics_pipeline(description);
  if (!pipeline.ok()) {
    return MakeFailure{MakeStep::Pipeline,
                       result_text(debug_name, pipeline.error(), pipeline.detail())};
  }
  out = std::move(pipeline.value());
  return {};
}

MakeFailure make_clamped_sampler(rhi::Device& device, rhi::Ref<rhi::Sampler>& out) {
  rhi::SamplerDescription description{};
  description.filter = rhi::Filter::Bilinear;
  description.address_u = rhi::AddressMode::Clamp;
  description.address_v = rhi::AddressMode::Clamp;
  description.address_w = rhi::AddressMode::Clamp;
  description.max_mip = 0.0F;
  rhi::SamplerResult sampler = device.sampler(description);
  if (!sampler.ok()) {
    return MakeFailure{MakeStep::State,
                       result_text("clamped sampler", sampler.error(), sampler.detail())};
  }
  out = std::move(sampler.value());
  return {};
}

} // namespace bw::postprocess::detail
