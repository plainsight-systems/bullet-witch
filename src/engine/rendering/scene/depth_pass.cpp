#include "bw/rendering/scene/depth_pass.hpp"

#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/materials/lit_template.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/culling.hpp"
#include "bw/rendering/scene/depth_shaders.hpp"
#include "bw/rendering/scene/draw_records.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Responsibility: the out-of-line part of bw/rendering/scene/depth_pass.hpp.
//
// A pipeline index's pipeline is made from the key's permutation of the lit
// template's depth shaders: the vertex stage always, the pixel stage only
// for a masked permutation (depth_shaders.hpp, contract 1). RecordDraw
// names the record buffer by the read index the culling frame carries. The
// depth and opaque passes draw the same culled records.
namespace bw::scene {

struct DepthPass::State {
  struct Record {
    State* state{};
    DepthDrawInputs inputs{};
    void operator()(graph::RecordContext& context) const;
  };

  std::filesystem::path shader_directory{};
  rhi::Format depth_format{rhi::Format::Unknown};
  rhi::Ref<rhi::RasterizerState> rasterizer{};
  rhi::Ref<rhi::DepthStencilState> depth{};
  rhi::Ref<rhi::BlendState> blend{};
  // By pipeline table index; null where the table's key has none yet.
  std::vector<rhi::Ref<rhi::GraphicsPipeline>> pipelines{};
  std::array<Record, 2> records{};
};

struct DepthPassAccess {
  using State = DepthPass::State;
  [[nodiscard]] static DepthPass::State& state(DepthPass& pass) noexcept { return *pass.state_; }
};

namespace {

template <class Code>
DepthPassFailure failed(DepthPassError error, std::uint32_t pipeline, std::string_view what,
                        Code code, std::string_view detail) {
  return DepthPassFailure{error, pipeline,
                          std::string(what) + ": error " +
                              std::to_string(static_cast<unsigned>(code)) + ": " +
                              std::string(detail)};
}

} // namespace

DepthPass::DepthPass() : state_(std::make_unique<State>()) {}
DepthPass::DepthPass(DepthPass&&) noexcept = default;
DepthPass& DepthPass::operator=(DepthPass&&) noexcept = default;
DepthPass::~DepthPass() = default;

DepthPassFailure DepthPass::prepare(rhi::Device& device, const PipelineTable& table) {
  State& state = *state_;
  const std::span<const PipelineKey> keys = table.keys();
  for (std::uint32_t index = 0; index < keys.size(); ++index) {
    if (index < state.pipelines.size() && state.pipelines[index]) {
      continue;
    }
    const PipelineKey& key = keys[index];
    const bool masked =
        materials::lit_switches(materials::LitDomain::from_id(key.permutation)).masked;
    shaders::ArtifactFile vertex;
    shaders::ArtifactFile pixel;
    // The artifact read last, which is the one a failure names.
    std::string_view artifact = kDepthVertexShader;
    shaders::ArtifactFileFailure read =
        shaders::read_artifact_file(state.shader_directory, artifact, key.permutation, vertex);
    if (read.ok() && masked) {
      artifact = kDepthPixelShader;
      read = shaders::read_artifact_file(state.shader_directory, artifact, key.permutation, pixel);
    }
    if (!read.ok()) {
      return DepthPassFailure{DepthPassError::ArtifactFailed, index,
                              shaders::artifact_file_failure_text(read, artifact)};
    }
    rhi::GraphicsPipelineDescription description{};
    description.vertex_shader = &vertex.artifact;
    description.pixel_shader = masked ? &pixel.artifact : nullptr;
    description.rasterizer = &*state.rasterizer;
    description.depth_stencil = &*state.depth;
    description.blend = &*state.blend;
    description.targets.color_count = 0;
    description.targets.depth_stencil_format = state.depth_format;
    description.targets.sample_count = 1;
    description.primitive = rhi::PrimitiveType::TriangleList;
    description.debug_name = "depth pass";
    rhi::GraphicsPipelineResult pipeline = device.graphics_pipeline(description);
    if (!pipeline.ok()) {
      return failed(DepthPassError::PipelineFailed, index, "graphics pipeline", pipeline.error(),
                    pipeline.detail());
    }
    if (state.pipelines.size() <= index) {
      state.pipelines.resize(index + 1U);
    }
    state.pipelines[index] = std::move(pipeline.value());
  }
  return {};
}

DepthPassFailure create_depth_pass(rhi::Device& device,
                                   const std::filesystem::path& shader_directory,
                                   rhi::Format depth_format, DepthPass& out) {
  out = DepthPass{};
  DepthPass built;
  DepthPassAccess::State& state = DepthPassAccess::state(built);
  state.shader_directory = shader_directory;
  state.depth_format = depth_format;
  rhi::RasterizerStateResult rasterizer = device.rasterizer_state(rhi::RasterizerDescription{});
  if (!rasterizer.ok()) {
    return failed(DepthPassError::StateFailed, 0, "rasterizer state", rasterizer.error(),
                  rasterizer.detail());
  }
  rhi::DepthStencilDescription depth{};
  depth.depth_write = true;
  depth.depth_test = rhi::CompareFunction::LessEqual;
  rhi::DepthStencilStateResult depth_state = device.depth_stencil_state(depth);
  if (!depth_state.ok()) {
    return failed(DepthPassError::StateFailed, 0, "depth stencil state", depth_state.error(),
                  depth_state.detail());
  }
  rhi::BlendStateResult blend = device.blend_state(rhi::BlendDescription{});
  if (!blend.ok()) {
    return failed(DepthPassError::StateFailed, 0, "blend state", blend.error(), blend.detail());
  }
  state.rasterizer = std::move(rasterizer.value());
  state.depth = std::move(depth_state.value());
  state.blend = std::move(blend.value());
  out = std::move(built);
  return {};
}

// Issues one multi-draw per pipeline over the compacted records.
void DepthPass::State::Record::operator()(graph::RecordContext& context) const {
  rhi::CommandList& list = context.list();
  rhi::Buffer* records = context.buffer(inputs.culling->records);
  rhi::Buffer* counts = context.buffer(inputs.culling->counts);
  list.set_static_block(kViewSlot, *inputs.view_block, 0);
  list.set_index_buffer(*inputs.geometry_buffers->indices, rhi::IndexFormat::Uint16, 0);
  const DrawListLayout& layout = inputs.culling->layout;
  for (std::uint32_t p = 0; p < layout.pipeline_count; ++p) {
    if (p >= state->pipelines.size() || !state->pipelines[p]) {
      std::fputs("bw::scene: a depth draw was declared for a pipeline index prepare made no "
                 "pipeline for (depth_pass.hpp, contract 3)\n",
                 stderr);
      std::abort();
    }
    const DrawList drawn = layout.list(kMainView, p, inputs.phase);
    if (drawn.capacity == 0U) {
      continue;
    }
    list.set_graphics_pipeline(*state->pipelines[p]);
    list.set_draw_block(RecordDraw{inputs.culling->records_index,
                                   static_cast<std::uint32_t>(drawn.record_offset),
                                   inputs.instance_buffer->view->buffer_index(), 0});
    list.draw_indexed_indirect_count(*records, *counts,
                                     rhi::MultiDrawArguments{drawn.record_offset, kDrawRecordBytes,
                                                             drawn.count_offset, drawn.capacity});
  }
}

void declare_depth_draw(DepthPass& pass, graph::FrameGraph& graph, const DepthDrawInputs& inputs) {
  DepthPassAccess::State& state = DepthPassAccess::state(pass);
  const std::size_t slot = inputs.phase == DrawPhase::PhaseOne ? 0U : 1U;
  state.records[slot] = DepthPassAccess::State::Record{&state, inputs};
  const std::array<graph::TextureUse, 1> textures{
      graph::TextureUse{inputs.depth, graph::Access::DepthWrite, inputs.contents, 0}};
  // The vertex stage reads each record's instance, and a masked pixel stage
  // its material through it.
  const std::array<graph::BufferUse, 3> buffers{
      graph::BufferUse{inputs.culling->records, graph::Access::Arguments},
      graph::BufferUse{inputs.culling->counts, graph::Access::Arguments},
      graph::BufferUse{inputs.instance_buffer->handle, graph::Access::Sampled}};
  graph.add_pass(graph::PassDeclaration{slot == 0U ? "depth pass one" : "depth pass two",
                                        graph::PassKind::Raster, textures, buffers,
                                        state.records[slot]});
}

} // namespace bw::scene
