// Unit test: public contracts of bw/rendering/rhi/pipeline_states.hpp, one
// scenario per CTest entry. Each scenario names the header declaration it asserts.
#include "bw/rendering/rhi/pipeline_states.hpp"

#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/shaders/artifact.hpp"
#include "bw/testing/harness.hpp"
#include "pipeline_objects.hpp"
#include "test_objects.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace {
using bw::rhi::DeferredDeleteQueue;
using bw::rhi::Format;
using bw::rhi::LoadAction;
using bw::rhi::Ref;
using bw::rhi::RenderPassDescription;
using bw::rhi::RenderPassError;
using bw::rhi::StoreAction;
using bw::rhi::TextureDescription;
using bw::rhi::TextureUsage;
using bw::testing::require;
using Scenario = bw::testing::Scenario<void>;
using bw_rhi_test::TestTexture;

TextureDescription depth_description(Format format) {
  TextureDescription d{};
  d.extent = bw::rhi::Extent3{64, 32, 1};
  d.format = format;
  d.usage = TextureUsage::DepthStencil;
  d.initial_access = bw::rhi::Access::DepthStencilWrite;
  d.debug_name = "Depth";
  return d;
}

struct Targets {
  DeferredDeleteQueue queue;
  Ref<TestTexture> color0 =
      bw_rhi_test::make<TestTexture>(queue, bw_rhi_test::render_target_description("Color0"));
  Ref<TestTexture> color1 = make_color1();
  Ref<TestTexture> depth =
      bw_rhi_test::make<TestTexture>(queue, depth_description(Format::D32Float));
  Ref<TestTexture> depth_stencil =
      bw_rhi_test::make<TestTexture>(queue, depth_description(Format::D24UnormS8Uint));
  Ref<TestTexture> sampled = make_sampled();
  Ref<TestTexture> small = make_small();
  Ref<TestTexture> multisampled = make_multisampled();

  [[nodiscard]] RenderPassDescription two_colors_and_depth() const {
    RenderPassDescription pass{};
    pass.color_count = 2;
    pass.colors[0].texture = color0.get();
    pass.colors[1].texture = color1.get();
    pass.depth_stencil.texture = depth.get();
    return pass;
  }

private:
  Ref<TestTexture> make_color1() {
    TextureDescription d = bw_rhi_test::render_target_description("Color1");
    d.format = Format::R16G16B16A16Float;
    d.mip_levels = 2;
    return bw_rhi_test::make<TestTexture>(queue, d);
  }
  Ref<TestTexture> make_sampled() {
    TextureDescription d = bw_rhi_test::render_target_description("Sampled");
    d.usage = TextureUsage::ShaderResource;
    d.initial_access = bw::rhi::Access::ShaderResourcePixel;
    return bw_rhi_test::make<TestTexture>(queue, d);
  }
  Ref<TestTexture> make_small() {
    TextureDescription d = bw_rhi_test::render_target_description("Small");
    d.extent = bw::rhi::Extent3{32, 16, 1};
    return bw_rhi_test::make<TestTexture>(queue, d);
  }
  Ref<TestTexture> make_multisampled() {
    TextureDescription d = bw_rhi_test::render_target_description("Multisampled");
    d.sample_count = 4;
    return bw_rhi_test::make<TestTexture>(queue, d);
  }
};

// (pipeline_states.hpp, validate_render_pass) and
// (pipeline_states.hpp, RenderPassError): color targets 0..n-1 without gaps,
// each with RenderTarget usage and a mip and slice inside it, one sample
// count and extent across the targets, stencil actions only on a stencil
// format, no clear on a read-only aspect, no load or store on a Memoryless
// target; the first violation is named.
void render_pass_validation_named() {
  const Targets t;
  require(bw::rhi::validate_render_pass(t.two_colors_and_depth()) == RenderPassError::None,
          "a valid pass");
  require(bw::rhi::validate_render_pass(RenderPassDescription{}) == RenderPassError::NoTargets,
          "no targets");
  RenderPassDescription depth_only{};
  depth_only.depth_stencil.texture = t.depth.get();
  require(bw::rhi::validate_render_pass(depth_only) == RenderPassError::None, "depth only");

  RenderPassDescription pass = t.two_colors_and_depth();
  pass.colors[0].texture = nullptr;
  require(bw::rhi::validate_render_pass(pass) == RenderPassError::ColorSlotGap, "slot 0 empty");
  pass = t.two_colors_and_depth();
  pass.color_count = 1;
  require(bw::rhi::validate_render_pass(pass) == RenderPassError::ColorSlotGap,
          "a target past color_count");

  pass = t.two_colors_and_depth();
  pass.colors[1].texture = t.sampled.get();
  require(bw::rhi::validate_render_pass(pass) == RenderPassError::TargetUsageMissing,
          "a color target without RenderTarget usage");
  pass = t.two_colors_and_depth();
  pass.depth_stencil.texture = t.color0.get();
  require(bw::rhi::validate_render_pass(pass) == RenderPassError::TargetUsageMissing,
          "a depth target without DepthStencil usage");

  pass = t.two_colors_and_depth();
  pass.colors[0].mip = 1;
  require(bw::rhi::validate_render_pass(pass) == RenderPassError::MipOrSliceOutOfRange,
          "a mip past the target's last");
  pass = t.two_colors_and_depth();
  pass.colors[0].slice = 1;
  require(bw::rhi::validate_render_pass(pass) == RenderPassError::MipOrSliceOutOfRange,
          "a slice past the target's last");

  pass = t.two_colors_and_depth();
  pass.colors[1].texture = t.multisampled.get();
  pass.colors[1].mip = 0;
  require(bw::rhi::validate_render_pass(pass) == RenderPassError::SampleCountMismatch,
          "one sample count across targets");

  pass = t.two_colors_and_depth();
  pass.colors[1].texture = t.small.get();
  require(bw::rhi::validate_render_pass(pass) == RenderPassError::ExtentMismatch,
          "one extent across targets");
  pass = t.two_colors_and_depth();
  pass.colors[0].texture = t.color1.get();
  pass.colors[0].mip = 1;
  pass.colors[1].texture = t.small.get();
  pass.depth_stencil.texture = nullptr;
  require(bw::rhi::validate_render_pass(pass) == RenderPassError::None,
          "extents compare at each target's mip");

  pass = t.two_colors_and_depth();
  pass.depth_stencil.stencil_load = LoadAction::Clear;
  require(bw::rhi::validate_render_pass(pass) == RenderPassError::StencilOnDepthOnlyFormat,
          "a stencil action on a depth-only format");
  pass.depth_stencil.texture = t.depth_stencil.get();
  require(bw::rhi::validate_render_pass(pass) == RenderPassError::None,
          "a stencil action on a stencil format");
  pass = t.two_colors_and_depth();
  pass.depth_stencil.depth_read_only = true;
  pass.depth_stencil.depth_load = LoadAction::Clear;
  require(bw::rhi::validate_render_pass(pass) == RenderPassError::ClearOnReadOnlyTarget,
          "a clear on a read-only depth");
  pass.depth_stencil.depth_load = LoadAction::Load;
  pass.depth_stencil.depth_store = StoreAction::NoAction;
  require(bw::rhi::validate_render_pass(pass) == RenderPassError::None, "a read-only load");
  DeferredDeleteQueue queue;
  TextureDescription memoryless = bw_rhi_test::render_target_description("Memoryless");
  memoryless.usage = TextureUsage::Memoryless | TextureUsage::RenderTarget;
  const Ref<TestTexture> memoryless_target = bw_rhi_test::make<TestTexture>(queue, memoryless);
  RenderPassDescription memoryless_pass{};
  memoryless_pass.color_count = 1;
  memoryless_pass.colors[0] =
      bw::rhi::ColorTarget{memoryless_target.get(), 0, 0, LoadAction::Clear, StoreAction::NoAction};
  require(bw::rhi::validate_render_pass(memoryless_pass) == RenderPassError::None,
          "a Memoryless target cleared and not stored");
  memoryless_pass.colors[0].store = StoreAction::Store;
  require(bw::rhi::validate_render_pass(memoryless_pass) == RenderPassError::MemorylessStored,
          "a Memoryless target stored");
}

// (pipeline_states.hpp, render_target_layout) and
// (pipeline_states.hpp, RenderTargetLayout): the layout a pipeline
// must match is the pass's color formats in slot order, its depth format
// and its sample count.
void render_target_layout_derived() {
  const Targets t;
  const bw::rhi::RenderTargetLayout layout =
      bw::rhi::render_target_layout(t.two_colors_and_depth());
  require(layout.color_count == 2U, "two color targets");
  require(layout.color_formats[0] == Format::R8G8B8A8Unorm &&
              layout.color_formats[1] == Format::R16G16B16A16Float,
          "color formats in slot order");
  require(layout.color_formats[2] == Format::Unknown, "unused slots are Unknown");
  require(layout.depth_stencil_format == Format::D32Float, "the depth format");
  require(layout.sample_count == 1U, "the sample count");

  RenderPassDescription depth_only{};
  depth_only.depth_stencil.texture = t.depth_stencil.get();
  const bw::rhi::RenderTargetLayout depth_layout = bw::rhi::render_target_layout(depth_only);
  require(depth_layout.color_count == 0U &&
              depth_layout.depth_stencil_format == Format::D24UnormS8Uint,
          "a depth-only layout");

  RenderPassDescription multisampled{};
  multisampled.color_count = 1;
  multisampled.colors[0].texture = t.multisampled.get();
  require(bw::rhi::render_target_layout(multisampled).sample_count == 4U,
          "the targets' sample count");
  require(bw::rhi::render_target_layout(t.two_colors_and_depth()) == layout,
          "the same pass gives the same layout");
}

// Shader artifacts and state objects one valid graphics description names.
struct PipelineParts {
  DeferredDeleteQueue queue;
  bw::shaders::Artifact vertex = bw_rhi_test::artifact(bw::shaders::ShaderStage::Vertex, 1);
  bw::shaders::Artifact pixel = bw_rhi_test::artifact(bw::shaders::ShaderStage::Pixel, 50);
  Ref<bw_rhi_test::TestRasterizerState> rasterizer =
      bw_rhi_test::make<bw_rhi_test::TestRasterizerState>(queue, bw::rhi::RasterizerDescription{});
  Ref<bw_rhi_test::TestDepthStencilState> depth_stencil =
      bw_rhi_test::make<bw_rhi_test::TestDepthStencilState>(queue,
                                                            bw::rhi::DepthStencilDescription{});
  Ref<bw_rhi_test::TestBlendState> blend =
      bw_rhi_test::make<bw_rhi_test::TestBlendState>(queue, bw::rhi::BlendDescription{});

  [[nodiscard]] bw::rhi::GraphicsPipelineDescription description() const {
    bw::rhi::GraphicsPipelineDescription d{};
    d.vertex_shader = &vertex;
    d.pixel_shader = &pixel;
    d.rasterizer = rasterizer.get();
    d.depth_stencil = depth_stencil.get();
    d.blend = blend.get();
    d.targets.color_count = 1;
    d.targets.color_formats[0] = Format::R8G8B8A8Unorm;
    d.debug_name = "Pipeline";
    return d;
  }
};

// (pipeline_states.hpp, validate_graphics_pipeline) and
// (pipeline_states.hpp, validate_compute_pipeline): the description-only checks, the first that
// applies in the stated order; the target is not among them.
void pipeline_validation_named() {
  using bw::rhi::PipelineError;
  const PipelineParts parts;
  require(bw::rhi::validate_graphics_pipeline(parts.description()) == PipelineError::None,
          "a valid description");
  bw::rhi::GraphicsPipelineDescription d = parts.description();
  d.vertex_shader = nullptr;
  d.rasterizer = nullptr;
  require(bw::rhi::validate_graphics_pipeline(d) == PipelineError::MissingVertexShader,
          "a missing vertex shader first");
  const bw::shaders::Artifact pixel_as_vertex =
      bw_rhi_test::artifact(bw::shaders::ShaderStage::Pixel, 2);
  d = parts.description();
  d.vertex_shader = &pixel_as_vertex;
  d.blend = nullptr;
  require(bw::rhi::validate_graphics_pipeline(d) == PipelineError::WrongStage,
          "a vertex slot holding a pixel artifact, before NullState");
  d = parts.description();
  d.pixel_shader = &parts.vertex;
  require(bw::rhi::validate_graphics_pipeline(d) == PipelineError::WrongStage,
          "a pixel slot holding a vertex artifact");
  d = parts.description();
  d.pixel_shader = nullptr;
  require(bw::rhi::validate_graphics_pipeline(d) == PipelineError::None, "depth only");
  d = parts.description();
  d.depth_stencil = nullptr;
  d.targets.color_count = 9;
  require(bw::rhi::validate_graphics_pipeline(d) == PipelineError::NullState,
          "a null state, before TooManyRenderTargets");
  d = parts.description();
  d.targets.color_count = 9;
  require(bw::rhi::validate_graphics_pipeline(d) == PipelineError::TooManyRenderTargets,
          "more than kMaxRenderTargets");
  bw::shaders::Artifact vulkan_vertex = parts.vertex;
  vulkan_vertex.header.target = bw::shaders::ShaderTarget::Vulkan;
  d = parts.description();
  d.vertex_shader = &vulkan_vertex;
  require(bw::rhi::validate_graphics_pipeline(d) == PipelineError::None,
          "the target is the device's check, not this function's");

  const bw::shaders::Artifact compute = bw_rhi_test::artifact(bw::shaders::ShaderStage::Compute, 3);
  require(bw::rhi::validate_compute_pipeline({&compute, "Compute"}) == PipelineError::None,
          "a valid compute description");
  require(bw::rhi::validate_compute_pipeline({nullptr, "Compute"}) ==
              PipelineError::MissingComputeShader,
          "a missing compute shader");
  require(bw::rhi::validate_compute_pipeline({&parts.vertex, "Compute"}) ==
              PipelineError::WrongStage,
          "a vertex artifact in the compute slot");
}

// (pipeline_states.hpp, GraphicsPipeline): GraphicsPipeline takes Refs on the three state
// objects, copies both artifacts' input hashes, drops the shader pointers,
// and description().debug_name views its own copy of the name.
void graphics_pipeline_holds_states_and_drops_shaders() {
  PipelineParts parts;
  std::string name = "Lit opaque";
  bw::rhi::GraphicsPipelineDescription d = parts.description();
  d.debug_name = name;
  require(parts.rasterizer->ref_count() == 1U, "one Ref before the pipeline");
  Ref<bw_rhi_test::TestGraphicsPipeline> pipeline =
      bw_rhi_test::make<bw_rhi_test::TestGraphicsPipeline>(parts.queue, d);
  name.assign("Changed name");
  require(parts.rasterizer->ref_count() == 2U && parts.depth_stencil->ref_count() == 2U &&
              parts.blend->ref_count() == 2U,
          "the pipeline holds a Ref to each state object");
  require(pipeline->description().vertex_shader == nullptr &&
              pipeline->description().pixel_shader == nullptr,
          "the shader pointers are dropped");
  require(pipeline->vertex_input_hash() == parts.vertex.header.input_hash &&
              pipeline->pixel_input_hash() == parts.pixel.header.input_hash,
          "the input hashes are copied");
  require(pipeline->description().rasterizer == parts.rasterizer.get(),
          "the description names the state objects");
#if BW_RHI_DEBUG
  require(pipeline->description().debug_name == "Lit opaque", "the debug name is a copy");
#else
  require(pipeline->description().debug_name.empty(), "no name in shipping");
#endif
  pipeline.reset();
  require(parts.queue.destroy_completed(parts.queue.current_frame()) == 1U,
          "the pipeline is destroyed");
  require(parts.rasterizer->ref_count() == 1U, "destroying the pipeline releases its Refs");
}

// (pipeline_states.hpp, ComputePipeline): ComputePipeline copies the artifact's input hash
// and thread group size, and keeps the binding it is given.
void compute_pipeline_copies_hash_and_group_size() {
  DeferredDeleteQueue queue;
  bw::shaders::Artifact compute = bw_rhi_test::artifact(bw::shaders::ShaderStage::Compute, 11);
  compute.reflection.thread_group_size = {8, 4, 2};
  const Ref<bw_rhi_test::TestComputePipeline> pipeline =
      bw_rhi_test::make<bw_rhi_test::TestComputePipeline>(
          queue, bw::rhi::ComputePipelineDescription{&compute, "Compute"});
  compute.reflection.thread_group_size = {1, 1, 1};
  require(pipeline->input_hash() == compute.header.input_hash, "the input hash");
  require(pipeline->thread_group_size() == (std::array<std::uint32_t, 3>{8, 4, 2}),
          "the thread group size was copied at construction");
  require(pipeline->binding() == bw::rhi::PipelineBinding{}, "the binding it was given");
}

constexpr std::array scenarios{
    Scenario{"render_pass_validation_named", render_pass_validation_named},
    Scenario{"render_target_layout_derived", render_target_layout_derived},
    Scenario{"pipeline_validation_named", pipeline_validation_named},
    Scenario{"graphics_pipeline_holds_states_and_drops_shaders",
             graphics_pipeline_holds_states_and_drops_shaders},
    Scenario{"compute_pipeline_copies_hash_and_group_size",
             compute_pipeline_copies_hash_and_group_size},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
