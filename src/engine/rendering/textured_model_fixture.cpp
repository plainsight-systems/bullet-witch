#include "bw/rendering/textured_model_fixture.hpp"

#include "bw/math/matrix/matrix4.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/mesh_resources.hpp"
#include "bw/rendering/geometry/textured_mesh_fixture.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/model_resources.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/texture_asset.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>

// Responsibility: the out-of-line part of
// bw/rendering/textured_model_fixture.hpp.
namespace bw::rendering {

namespace {

template <class Code>
TexturedModelFailure failed(TexturedModelError error, std::uint32_t index, std::string_view what,
                            Code code, std::string_view detail) {
  std::string text(what);
  text += ": error " + std::to_string(static_cast<unsigned>(code));
  if (!detail.empty()) {
    text += ": ";
    text += detail;
  }
  return TexturedModelFailure{error, index, std::move(text)};
}

// The 1x1 opaque white texture a draw without a base color samples.
TexturedModelFailure create_white(rhi::Device& device, TexturedModel& built) {
  rhi::TextureDescription white{};
  white.extent = rhi::Extent3{1, 1, 1};
  white.format = rhi::Format::R8G8B8A8Unorm;
  white.usage = rhi::TextureUsage::ShaderResource;
  white.initial_access = rhi::Access::ShaderResourcePixel;
  white.debug_name = "textured model white base color";
  const std::array<std::byte, 4> texel{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                       std::byte{0xFF}};
  const std::array<rhi::SubresourceData, 1> data{rhi::SubresourceData{texel, 4, 0}};
  rhi::TextureResult created = device.create_texture(white, data);
  if (!created.ok()) {
    return failed(TexturedModelError::WhiteTextureFailed, 0, "white texture", created.error(),
                  created.detail());
  }
  rhi::ShaderResourceViewResult viewed =
      device.create_shader_resource_view(created.value(), rhi::TextureViewDescription{});
  if (!viewed.ok()) {
    return failed(TexturedModelError::WhiteTextureFailed, 0, "white texture view", viewed.error(),
                  viewed.detail());
  }
  built.white = std::move(created.value());
  built.white_view = std::move(viewed.value());
  return {};
}

TexturedModelFailure resolve_base_colors(rhi::Device& device, const ModelResources& model,
                                         TexturedModel& built) {
  built.draw_base_colors.reserve(model.mesh.draws.size());
  for (std::uint32_t d = 0; d < model.mesh.draws.size(); ++d) {
    const std::uint32_t material = model.mesh.draws[d].material;
    const ModelTexture* base = material == geometry::kNoMaterial
                                   ? nullptr
                                   : model.texture(material, materials::kBaseColorTexture);
    if (base != nullptr) {
      if (base->dimension != TextureAssetDimension::Texture2D) {
        return TexturedModelFailure{TexturedModelError::BaseColorNotTexture2D, d,
                                    std::string(base->id.view())};
      }
      built.draw_base_colors.push_back(base->view->texture_index());
      continue;
    }
    if (!built.white) {
      TexturedModelFailure failure = create_white(device, built);
      if (!failure.ok()) {
        return failure;
      }
    }
    built.draw_base_colors.push_back(built.white_view->texture_index());
    if (built.fallback_draws == 0U) {
      built.first_fallback_draw = d;
    }
    ++built.fallback_draws;
  }
  return {};
}

TexturedModelFailure create_states(rhi::Device& device, TexturedModel& built) {
  rhi::RasterizerStateResult rasterizer = device.rasterizer_state(rhi::RasterizerDescription{});
  if (!rasterizer.ok()) {
    return failed(TexturedModelError::StateFailed, 0, "rasterizer state", rasterizer.error(),
                  rasterizer.detail());
  }
  built.rasterizer = std::move(rasterizer.value());
  rhi::DepthStencilDescription depth{};
  depth.depth_write = true;
  depth.depth_test = rhi::CompareFunction::LessEqual;
  rhi::DepthStencilStateResult depth_stencil = device.depth_stencil_state(depth);
  if (!depth_stencil.ok()) {
    return failed(TexturedModelError::StateFailed, 0, "depth stencil state", depth_stencil.error(),
                  depth_stencil.detail());
  }
  built.depth_stencil = std::move(depth_stencil.value());
  rhi::BlendStateResult blend = device.blend_state(rhi::BlendDescription{});
  if (!blend.ok()) {
    return failed(TexturedModelError::StateFailed, 0, "blend state", blend.error(), blend.detail());
  }
  built.blend = std::move(blend.value());
  rhi::SamplerDescription trilinear{};
  trilinear.filter = rhi::Filter::Trilinear;
  rhi::SamplerResult sampler = device.sampler(trilinear);
  if (!sampler.ok()) {
    return failed(TexturedModelError::StateFailed, 0, "sampler", sampler.error(), sampler.detail());
  }
  built.sampler = std::move(sampler.value());
  return {};
}

} // namespace

TexturedModelFailure create_textured_model(rhi::Device& device, const ModelResources& model,
                                           // The two artifacts in pipeline order.
                                           // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
                                           const shaders::Artifact& vertex,
                                           const shaders::Artifact& pixel, rhi::Format color_format,
                                           TexturedModel& out) {
  out = TexturedModel{};
  for (std::uint32_t g = 0; g < model.mesh.groups.size(); ++g) {
    if (!geometry::textured_pair_reads(model.mesh.groups[g].layout)) {
      return TexturedModelFailure{
          TexturedModelError::GroupLayoutUnsupported, g,
          "the textured pair reads position float3, normal short4n and texture coordinate 1 "
          "half2"};
    }
  }
  TexturedModel built;
  TexturedModelFailure failure = resolve_base_colors(device, model, built);
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
  description.targets.depth_stencil_format = kTexturedModelDepthFormat;
  description.targets.sample_count = 1;
  description.primitive = rhi::PrimitiveType::TriangleList;
  description.debug_name = "textured model fixture";
  const rhi::Status<rhi::PipelineError> precompiled =
      device.precompile(std::span<const rhi::GraphicsPipelineDescription>{&description, 1}, {});
  if (!precompiled.ok()) {
    return failed(TexturedModelError::PipelineFailed, 0, "pipeline precompile", precompiled.error(),
                  precompiled.detail());
  }
  rhi::GraphicsPipelineResult pipeline = device.graphics_pipeline(description);
  if (!pipeline.ok()) {
    return failed(TexturedModelError::PipelineFailed, 0, "graphics pipeline", pipeline.error(),
                  pipeline.detail());
  }
  built.pipeline = std::move(pipeline.value());
  out = std::move(built);
  return {};
}

void record_textured_model(rhi::CommandList& list, const TexturedModel& textured,
                           const ModelResources& model, const math::Matrix4f& world_to_clip) {
  geometry::TexturedMeshDraw block{};
  block.world_to_clip = scene::shader_matrix(world_to_clip);
  block.sampler = textured.sampler->index();
  list.set_graphics_pipeline(*textured.pipeline);
  auto bound_group = static_cast<std::uint32_t>(model.mesh.groups.size());
  for (std::size_t d = 0; d < model.mesh.draws.size(); ++d) {
    const geometry::MeshDrawRange& draw = model.mesh.draws[d];
    const geometry::MeshGroupResources& group = model.mesh.groups[draw.group];
    if (draw.group != bound_group) {
      list.set_index_buffer(*group.indices, rhi::IndexFormat::Uint16, 0);
      bound_group = draw.group;
    }
    block.streams = group.block;
    block.streams.base_vertex = draw.base_vertex;
    block.base_color = textured.draw_base_colors[d];
    list.set_draw_block(block);
    list.draw_indexed(rhi::DrawIndexedArguments{draw.index_count, 1, draw.first_index, 0, 0});
  }
}

} // namespace bw::rendering
