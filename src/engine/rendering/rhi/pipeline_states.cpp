#include "bw/rendering/rhi/pipeline_states.hpp"

#include <cstdint>

namespace bw::rhi {

namespace {

// Slices a render target's slice index is checked against at one mip.
[[nodiscard]] std::uint32_t target_slices(const TextureDescription& d, std::uint32_t mip) noexcept {
  switch (d.dimension) {
  case TextureDimension::TextureCube:
    return 6U * d.array_size;
  case TextureDimension::Texture3D: {
    const std::uint32_t depth = d.extent.depth >> mip;
    return depth == 0U ? 1U : depth;
  }
  case TextureDimension::Texture2D:
  case TextureDimension::Texture2DArray:
    return d.array_size;
  }
  return d.array_size;
}

[[nodiscard]] Extent2 extent_at(const TextureDescription& d, std::uint32_t mip) noexcept {
  const std::uint32_t width = d.extent.width >> mip;
  const std::uint32_t height = d.extent.height >> mip;
  return Extent2{width == 0U ? 1U : width, height == 0U ? 1U : height};
}

} // namespace

// Walks the color slots until the first empty one, then requires every
// later slot empty (no gaps), and requires one sample count across the color
// and depth targets, checked in RenderPassError's order: every target is
// checked for one code before any target is checked for the next. A pass
// with no depth target is accepted whatever its depth and stencil actions
// and read-only flags: with no depth texture they address nothing, and the
// backend reads none of them.
RenderPassError validate_render_pass(const RenderPassDescription& pass) noexcept {
  const Texture* depth = pass.depth_stencil.texture;
  if (pass.color_count == 0U && depth == nullptr) {
    return RenderPassError::NoTargets;
  }
  if (pass.color_count > kMaxRenderTargets) {
    return RenderPassError::ColorSlotGap;
  }
  for (std::uint32_t i = 0; i < kMaxRenderTargets; ++i) {
    const bool named = i < pass.color_count;
    if (named != (pass.colors[i].texture != nullptr)) {
      return RenderPassError::ColorSlotGap;
    }
  }
  for (std::uint32_t i = 0; i < pass.color_count; ++i) {
    if (!has_usage(pass.colors[i].texture->description().usage, TextureUsage::RenderTarget)) {
      return RenderPassError::TargetUsageMissing;
    }
  }
  if (depth != nullptr && !has_usage(depth->description().usage, TextureUsage::DepthStencil)) {
    return RenderPassError::TargetUsageMissing;
  }
  for (std::uint32_t i = 0; i < pass.color_count; ++i) {
    const ColorTarget& target = pass.colors[i];
    const TextureDescription& d = target.texture->description();
    if (target.mip >= d.mip_levels || target.slice >= target_slices(d, target.mip)) {
      return RenderPassError::MipOrSliceOutOfRange;
    }
  }
  // The first target's sample count and extent are the pass's.
  const TextureDescription& first =
      pass.color_count > 0U ? pass.colors[0].texture->description() : depth->description();
  const std::uint32_t first_mip = pass.color_count > 0U ? pass.colors[0].mip : 0U;
  for (std::uint32_t i = 0; i < pass.color_count; ++i) {
    if (pass.colors[i].texture->description().sample_count != first.sample_count) {
      return RenderPassError::SampleCountMismatch;
    }
  }
  if (depth != nullptr && depth->description().sample_count != first.sample_count) {
    return RenderPassError::SampleCountMismatch;
  }
  const Extent2 extent = extent_at(first, first_mip);
  for (std::uint32_t i = 0; i < pass.color_count; ++i) {
    if (extent_at(pass.colors[i].texture->description(), pass.colors[i].mip) != extent) {
      return RenderPassError::ExtentMismatch;
    }
  }
  if (depth != nullptr && extent_at(depth->description(), 0U) != extent) {
    return RenderPassError::ExtentMismatch;
  }
  if (depth != nullptr) {
    const DepthStencilTarget& target = pass.depth_stencil;
    if (!format_info(depth->description().format).has_stencil &&
        (target.stencil_load != LoadAction::NoAction ||
         target.stencil_store != StoreAction::NoAction)) {
      return RenderPassError::StencilOnDepthOnlyFormat;
    }
    if ((target.depth_read_only && target.depth_load == LoadAction::Clear) ||
        (target.stencil_read_only && target.stencil_load == LoadAction::Clear)) {
      return RenderPassError::ClearOnReadOnlyTarget;
    }
  }
  return RenderPassError::None;
}

RenderTargetLayout render_target_layout(const RenderPassDescription& pass) noexcept {
  RenderTargetLayout layout{};
  layout.color_count = pass.color_count;
  for (std::uint32_t i = 0; i < pass.color_count; ++i) {
    layout.color_formats[i] = pass.colors[i].texture->description().format;
  }
  const Texture* depth = pass.depth_stencil.texture;
  if (depth != nullptr) {
    layout.depth_stencil_format = depth->description().format;
  }
  const Texture* first = pass.color_count > 0U ? pass.colors[0].texture : depth;
  if (first != nullptr) {
    layout.sample_count = first->description().sample_count;
  }
  return layout;
}

// The checks in the order pipeline_states.hpp states; the first that
// applies is returned.
PipelineError validate_graphics_pipeline(const GraphicsPipelineDescription& description) noexcept {
  if (description.vertex_shader == nullptr) {
    return PipelineError::MissingVertexShader;
  }
  if (description.vertex_shader->header.stage != shaders::ShaderStage::Vertex ||
      (description.pixel_shader != nullptr &&
       description.pixel_shader->header.stage != shaders::ShaderStage::Pixel)) {
    return PipelineError::WrongStage;
  }
  if (description.rasterizer == nullptr || description.depth_stencil == nullptr ||
      description.blend == nullptr) {
    return PipelineError::NullState;
  }
  if (description.targets.color_count > kMaxRenderTargets) {
    return PipelineError::TooManyRenderTargets;
  }
  return PipelineError::None;
}

PipelineError validate_compute_pipeline(const ComputePipelineDescription& description) noexcept {
  if (description.compute_shader == nullptr) {
    return PipelineError::MissingComputeShader;
  }
  if (description.compute_shader->header.stage != shaders::ShaderStage::Compute) {
    return PipelineError::WrongStage;
  }
  return PipelineError::None;
}

// The shader pointers are dropped (the pipeline does not keep the
// artifacts alive); the debug name is copied into name_ and
// description().debug_name views that copy, as Buffer does.
GraphicsPipeline::GraphicsPipeline(DeferredDeleteQueue& queue,
                                   const GraphicsPipelineDescription& description,
                                   const PipelineBinding& binding) noexcept
    : Resource(queue), description_(description), name_(description.debug_name), binding_(binding),
      rasterizer_(description.rasterizer), depth_stencil_(description.depth_stencil),
      blend_(description.blend) {
  if (description.vertex_shader != nullptr) {
    vertex_hash_ = description.vertex_shader->header.input_hash;
  }
  if (description.pixel_shader != nullptr) {
    pixel_hash_ = description.pixel_shader->header.input_hash;
  }
  description_.vertex_shader = nullptr;
  description_.pixel_shader = nullptr;
  description_.debug_name = name_.view();
}

ComputePipeline::ComputePipeline(DeferredDeleteQueue& queue,
                                 const ComputePipelineDescription& description,
                                 const PipelineBinding& binding) noexcept
    : Resource(queue), name_(description.debug_name), binding_(binding) {
  if (description.compute_shader != nullptr) {
    hash_ = description.compute_shader->header.input_hash;
    thread_group_ = description.compute_shader->reflection.thread_group_size;
  }
}

} // namespace bw::rhi
