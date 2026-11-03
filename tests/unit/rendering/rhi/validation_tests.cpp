// Unit test: public contract of the validation layer, backend.hpp
// (ValidationCode, ValidationFailure, ValidationReporter,
// wrap_with_validation and the contract comment above them), one scenario
// per CTest entry. The layer is reached only through wrap_with_validation
// and the Backend and Context interfaces; the backend it wraps is the
// recording fake of validation_objects.hpp, the boundary the contract names
// ("the layer never changes what the backend sees"). Each scenario names the
// header declaration it asserts.
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/binding_structure.hpp"
#include "bw/rendering/rhi/build.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/descriptor_allocator.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/queries.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/transitions.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/root_layout.hpp"
#include "test_main.hpp"
#include "test_objects.hpp"
#include "validation_objects.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#if !BW_RHI_DEBUG
#error "rhi_validation_tests is built only with BW_RHI_DEBUG 1"
#endif

namespace {
using bw::rhi::Access;
using bw::rhi::Ref;
using bw::rhi::Transition;
using bw::rhi::TransitionBatch;
using bw::rhi::TransitionError;
using bw::rhi::TransitionPipelines;
using bw::rhi::ValidationCode;
using bw::rhi::ValidationFailure;
using bw_rhi_test::require;
using bw_rhi_validation_test::Args;
using bw_rhi_validation_test::describe;

// One wrapped recording backend and what its reporter captured. The queue
// is declared first so it outlives every Ref a scenario holds.
struct Fixture {
  explicit Fixture(bool async_compute = false) {
    bw::rhi::Capabilities capabilities{};
    capabilities.typed_descriptor_arrays = true;
    capabilities.shader_floor = true;
    capabilities.async_compute = async_compute;
    auto recording = std::make_unique<bw_rhi_validation_test::RecordingBackend>(capabilities);
    inner = recording.get();
    backend = bw::rhi::wrap_with_validation(std::move(recording), &bw_rhi_validation_test::capture,
                                            &captured);
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  ~Fixture() = default;

  [[nodiscard]] bw::rhi::Context& context() const { return backend->context(); }
  [[nodiscard]] std::size_t context_calls() const {
    return inner->recording_context().calls.size();
  }

  bw::rhi::DeferredDeleteQueue queue;
  bw_rhi_validation_test::Captured captured;
  bw_rhi_validation_test::RecordingBackend* inner{};
  std::unique_ptr<bw::rhi::Backend> backend;
};

Ref<bw_rhi_test::TestBuffer> buffer(Fixture& f, std::string_view name, Access initial) {
  return bw_rhi_test::make<bw_rhi_test::TestBuffer>(
      f.queue, bw::rhi::BufferDescription{.size = 256,
                                          .stride = 4,
                                          .usage = bw::rhi::BufferUsage::Vertex |
                                                   bw::rhi::BufferUsage::CopySource |
                                                   bw::rhi::BufferUsage::CopyDestination,
                                          .initial_access = initial,
                                          .debug_name = name});
}

Ref<bw_rhi_test::TestTexture> texture(Fixture& f, std::string_view name, Access initial) {
  bw::rhi::TextureDescription description = bw_rhi_test::render_target_description(name);
  description.usage = bw::rhi::TextureUsage::RenderTarget | bw::rhi::TextureUsage::CopySource |
                      bw::rhi::TextureUsage::CopyDestination;
  description.initial_access = initial;
  return bw_rhi_test::make<bw_rhi_test::TestTexture>(f.queue, description);
}

bw::rhi::RenderPassDescription pass_for(bw::rhi::Texture& target,
                                        bw::rhi::QueryPool* queries = nullptr) {
  bw::rhi::RenderPassDescription pass{};
  pass.color_count = 1;
  pass.colors[0].texture = &target;
  pass.occlusion_queries = queries;
  pass.name = "Main pass";
  return pass;
}

// A graphics pipeline built for one R8G8B8A8Unorm target unless `format`
// says otherwise.
Ref<bw_rhi_validation_test::BoundGraphicsPipeline>
graphics_pipeline(Fixture& f, std::string_view name, bw::rhi::PipelineBinding binding = {},
                  bw::rhi::Format format = bw::rhi::Format::R8G8B8A8Unorm) {
  bw::rhi::GraphicsPipelineDescription description{};
  description.targets.color_count = 1;
  description.targets.color_formats[0] = format;
  description.debug_name = name;
  return bw_rhi_test::make<bw_rhi_validation_test::BoundGraphicsPipeline>(f.queue, description,
                                                                          binding);
}

bw::rhi::PipelineBinding draw_block_binding(std::uint64_t hash) {
  bw::rhi::PipelineBinding binding{};
  binding.draw_block_hash = bw::shaders::LayoutHash{hash};
  binding.uses_draw_block = true;
  return binding;
}

// Exactly one failure arrived since `before`, with these fields.
void require_reported(const Fixture& f, std::size_t before, ValidationCode code,
                      TransitionError transition, std::string_view resource,
                      std::string_view command) {
  require(f.captured.count == before + 1, std::string("exactly one failure expected from ") +
                                              std::string(command) + ", got " +
                                              std::to_string(f.captured.count - before));
  const ValidationFailure& failure = f.captured.failures[before];
  require(failure.code == code, std::string("code from ") + std::string(command));
  require(failure.transition == transition,
          std::string("transition error from ") + std::string(command));
  require(failure.resource_name == resource, std::string("resource name from ") +
                                                 std::string(command) + ": '" +
                                                 std::string(failure.resource_name) + "'");
  require(failure.command == command,
          std::string("command name: '") + std::string(failure.command) + "'");
}

void require_quiet(const Fixture& f, std::size_t before, std::string_view step) {
  require(f.captured.count == before, std::string("no failure expected from ") + std::string(step));
}

// The inner context received exactly one more call, named `name`.
void require_forwarded(const Fixture& f, std::size_t calls_before, std::string_view name) {
  const auto& calls = f.inner->recording_context().calls;
  require(calls.size() == calls_before + 1,
          std::string("forwarded exactly once: ") + std::string(name));
  require(calls.back().name == name, std::string("forwarded as ") + std::string(name));
}

// (backend.hpp, wrap_with_validation: the validation layer checks every
// declared transition against transitions.hpp and reports a failure once with
// the code, the resource's debug name and the command's name, then forwards);
// (transitions.hpp, validate_begin, TransitionError::BeforeDoesNotMatchTracked:
// a before that differs from the tracked access is BeforeDoesNotMatchTracked).
void undeclared_transition_rejected_by_name() {
  Fixture f;
  const auto vertices = buffer(f, "Vertices", Access::VertexOrIndexBuffer);
  const std::array transitions{
      Transition{vertices.get(), Access::CopySource, Access::CopyDestination}};
  const TransitionBatch batch{transitions, {}};
  const std::size_t calls = f.context_calls();
  f.context().begin_transitions(batch);
  require_reported(f, 0, ValidationCode::Transition, TransitionError::BeforeDoesNotMatchTracked,
                   "Vertices", "begin_transitions");
  require_forwarded(f, calls, "begin_transitions");
}

// (backend.hpp, ValidationCode::Redundant: a redundant transition is reported
// with Redundant and forwarded); (transitions.hpp, is_redundant: before equal
// to after is redundant, not an error). The matching end is not a failure.
void redundant_transition_reported_and_forwarded() {
  Fixture f;
  const auto target = texture(f, "Target", Access::RenderTarget);
  const std::array transitions{Transition{target.get(), Access::Unknown, Access::RenderTarget}};
  const TransitionBatch batch{transitions, {}};
  f.context().begin_transitions(batch);
  require_reported(f, 0, ValidationCode::Redundant, TransitionError::None, "Target",
                   "begin_transitions");
  f.context().end_transitions(batch);
  require_quiet(f, 1, "end_transitions of the redundant transition");
  const auto& calls = f.inner->recording_context().calls;
  require(calls.size() == 2, "both commands forwarded");
  require(calls[0].name == "begin_transitions" && calls[0].args == describe(batch),
          "begin forwarded with the batch");
  require(calls[1].name == "end_transitions" && calls[1].args == describe(batch),
          "end forwarded with the batch");
}

// (transitions.hpp, validate_begin, validate_end: a begin while a transition
// is open is TransitionAlreadyOpen; an end with none open is NoOpenTransition;
// an end whose after differs from the begin is EndDoesNotMatchBegin), reported
// by the layer per (backend.hpp, wrap_with_validation).
void double_begin_and_unsolicited_end_named() {
  Fixture f;
  const auto first = buffer(f, "First", Access::CopySource);
  const auto second = buffer(f, "Second", Access::CopySource);
  const auto third = buffer(f, "Third", Access::CopySource);

  const std::array open{Transition{first.get(), Access::Unknown, Access::CopyDestination}};
  f.context().begin_transitions(TransitionBatch{open, {}});
  require_quiet(f, 0, "first begin");
  f.context().begin_transitions(TransitionBatch{open, {}});
  require_reported(f, 0, ValidationCode::Transition, TransitionError::TransitionAlreadyOpen,
                   "First", "begin_transitions");

  const std::array unsolicited{Transition{second.get(), Access::Unknown, Access::CopyDestination}};
  f.context().end_transitions(TransitionBatch{unsolicited, {}});
  require_reported(f, 1, ValidationCode::Transition, TransitionError::NoOpenTransition, "Second",
                   "end_transitions");

  const std::array begin{Transition{third.get(), Access::Unknown, Access::CopyDestination}};
  const std::array end{Transition{third.get(), Access::Unknown, Access::ShaderResourcePixel}};
  f.context().begin_transitions(TransitionBatch{begin, {}});
  require_quiet(f, 2, "third begin");
  f.context().end_transitions(TransitionBatch{end, {}});
  require_reported(f, 2, ValidationCode::Transition, TransitionError::EndDoesNotMatchBegin, "Third",
                   "end_transitions");
}

// (transitions.hpp, TransitionError::SourceDestinationMismatch: a split batch
// is rejected on a device without async compute);
// (backend.hpp, wrap_with_validation: a transition batch is reported once per
// failing transition, each with that transition's resource name);
// (backend.hpp, Capabilities::async_compute: the capability report's
// async_compute). The layer reports it at the begin; the same batch on a
// device with async compute is accepted.
void split_batch_on_single_queue_is_named() {
  const TransitionPipelines split{bw::rhi::Pipeline::Graphics, bw::rhi::Pipeline::AsyncCompute};
  {
    Fixture f(false);
    const auto a = buffer(f, "A", Access::CopySource);
    const auto b = buffer(f, "B", Access::CopySource);
    const std::array transitions{
        Transition{a.get(), Access::Unknown, Access::UnorderedAccessCompute},
        Transition{b.get(), Access::Unknown, Access::UnorderedAccessCompute}};
    f.context().begin_transitions(TransitionBatch{transitions, split});
    require(f.captured.count == 2, "one failure per transition of the split batch");
    for (std::size_t i = 0; i < 2; ++i) {
      const ValidationFailure& failure = f.captured.failures[i];
      require(failure.code == ValidationCode::Transition &&
                  failure.transition == TransitionError::SourceDestinationMismatch,
              "SourceDestinationMismatch");
      require(failure.command == "begin_transitions", "named at the begin");
    }
    require(f.captured.failures[0].resource_name == "A" &&
                f.captured.failures[1].resource_name == "B",
            "each resource named");
    f.context().end_transitions(TransitionBatch{transitions, split});
    require_quiet(f, 2, "end of the split batch");
  }
  {
    Fixture f(true);
    const auto a = buffer(f, "A", Access::CopySource);
    const std::array transitions{
        Transition{a.get(), Access::Unknown, Access::UnorderedAccessCompute}};
    f.context().begin_transitions(TransitionBatch{transitions, split});
    f.context().end_transitions(TransitionBatch{transitions, split});
    require_quiet(f, 0, "split batch with async compute");
  }
}

// (transitions.hpp, validate_use: at use, a copy source must be in
// CopySource: AccessMissingAtUse); (backend.hpp, wrap_with_validation).
// Control: once a declared transition to CopySource has ended, the same copy
// is accepted (transitions.hpp, apply_end: the end writes the tracked access
// the next check reads).
void use_without_access_is_named() {
  Fixture f;
  const auto source = buffer(f, "Source", Access::ShaderResourcePixel);
  const auto destination = buffer(f, "Destination", Access::CopyDestination);
  const std::size_t calls = f.context_calls();
  f.context().copy_buffer(*source, *destination, bw::rhi::BufferCopy{0, 0, 16});
  require_reported(f, 0, ValidationCode::Transition, TransitionError::AccessMissingAtUse, "Source",
                   "copy_buffer");
  require_forwarded(f, calls, "copy_buffer");

  const std::array transitions{Transition{source.get(), Access::Unknown, Access::CopySource}};
  f.context().begin_transitions(TransitionBatch{transitions, {}});
  f.context().end_transitions(TransitionBatch{transitions, {}});
  f.context().copy_buffer(*source, *destination, bw::rhi::BufferCopy{0, 0, 16});
  require_quiet(f, 1, "copy after the declared transition to CopySource");
}

// (command_list.hpp, CommandList::begin_render_pass: passes nest nothing;
// draws only inside a pass; copies only outside one), reported by name per
// (backend.hpp, wrap_with_validation): EndWithoutBegin,
// CommandOutsideRenderPass, NestedRenderPass, CommandInsideRenderPass.
void render_pass_nesting_named() {
  Fixture f;
  const auto target = texture(f, "Target", Access::RenderTarget);
  const auto source = buffer(f, "Source", Access::CopySource);
  const auto destination = buffer(f, "Destination", Access::CopyDestination);
  const bw::rhi::RenderPassDescription pass = pass_for(*target);

  f.context().end_render_pass();
  require_reported(f, 0, ValidationCode::EndWithoutBegin, TransitionError::None, "",
                   "end_render_pass");
  f.context().draw(bw::rhi::DrawArguments{3});
  require_reported(f, 1, ValidationCode::CommandOutsideRenderPass, TransitionError::None, "",
                   "draw");
  f.context().begin_render_pass(pass);
  require_quiet(f, 2, "first begin_render_pass");
  f.context().begin_render_pass(pass);
  require_reported(f, 2, ValidationCode::NestedRenderPass, TransitionError::None, "Main pass",
                   "begin_render_pass");
  f.context().copy_buffer(*source, *destination, bw::rhi::BufferCopy{0, 0, 16});
  require_reported(f, 3, ValidationCode::CommandInsideRenderPass, TransitionError::None, "Source",
                   "copy_buffer");
  f.context().end_render_pass();
  require_quiet(f, 4, "end_render_pass after begin");
}

// (backend.hpp, wrap_with_validation: every render pass against
// validate_render_pass);
// (pipeline_states.hpp, validate_render_pass, RenderPassError::TargetUsageMissing:
// a color target without RenderTarget usage is named): RenderPassInvalid, with
// the pass's name.
void render_pass_description_invalid_named() {
  Fixture f;
  bw::rhi::TextureDescription description = bw_rhi_test::render_target_description("Sampled");
  description.usage = bw::rhi::TextureUsage::ShaderResource;
  description.initial_access = Access::RenderTarget;
  const auto sampled = bw_rhi_test::make<bw_rhi_test::TestTexture>(f.queue, description);
  const bw::rhi::RenderPassDescription pass = pass_for(*sampled);
  require(bw::rhi::validate_render_pass(pass) != bw::rhi::RenderPassError::None,
          "the description is one validate_render_pass rejects");
  const std::size_t calls = f.context_calls();
  f.context().begin_render_pass(pass);
  require_reported(f, 0, ValidationCode::RenderPassInvalid, TransitionError::None, "Main pass",
                   "begin_render_pass");
  require_forwarded(f, calls, "begin_render_pass");
}

// (backend.hpp, wrap_with_validation: every pipeline against the pass it is
// set in, render_target_layout: PipelineTargetsMismatch);
// (pipeline_states.hpp, render_target_layout);
// (command_list.hpp, CommandList::set_graphics_pipeline: a draw with no
// graphics pipeline set in the pass): NoPipelineBound.
void pipeline_targets_mismatch_and_no_pipeline_named() {
  Fixture f;
  const auto target = texture(f, "Target", Access::RenderTarget);
  const auto matching = graphics_pipeline(f, "Matching");
  const auto mismatched =
      graphics_pipeline(f, "Mismatched", {}, bw::rhi::Format::R16G16B16A16Float);
  f.context().begin_render_pass(pass_for(*target));
  f.context().draw(bw::rhi::DrawArguments{3});
  require_reported(f, 0, ValidationCode::NoPipelineBound, TransitionError::None, "", "draw");
  f.context().set_graphics_pipeline(*mismatched);
  require_reported(f, 1, ValidationCode::PipelineTargetsMismatch, TransitionError::None,
                   "Mismatched", "set_graphics_pipeline");
  f.context().set_graphics_pipeline(*matching);
  f.context().draw(bw::rhi::DrawArguments{3});
  require_quiet(f, 2, "matching pipeline and draw");
  f.context().end_render_pass();
  // A new pass starts with no pipeline set.
  f.context().begin_render_pass(pass_for(*target));
  f.context().draw(bw::rhi::DrawArguments{3});
  require_reported(f, 2, ValidationCode::NoPipelineBound, TransitionError::None, "", "draw");
}

// (backend.hpp, wrap_with_validation: every draw block against the pipeline's
// draw block hash; the failure names the pipeline: the graphics description's
// debug name, ComputePipeline::debug_name);
// (command_list.hpp, CommandList::set_draw_block: blocks bind to the pass's
// graphics pipeline inside a pass and to the compute pipeline outside one; the
// layout hash travels with the bytes); (binding_structure.hpp, PipelineBinding).
// A pipeline that reads no draw block matches no layout.
void draw_block_layout_mismatch_named() {
  Fixture f;
  const auto target = texture(f, "Target", Access::RenderTarget);
  const auto pipeline = graphics_pipeline(f, "Pipeline", draw_block_binding(0x1234U));
  const auto compute = bw_rhi_test::make<bw_rhi_validation_test::BoundComputePipeline>(
      f.queue, draw_block_binding(0x55U), "Compute");
  const auto blockless = graphics_pipeline(f, "Blockless");
  const std::array<std::byte, 16> bytes{};

  f.context().begin_render_pass(pass_for(*target));
  f.context().set_graphics_pipeline(*pipeline);
  static_cast<void>(f.context().set_draw_block(bytes, bw::shaders::LayoutHash{0x9999U}));
  require_reported(f, 0, ValidationCode::DrawBlockLayoutMismatch, TransitionError::None, "Pipeline",
                   "set_draw_block");
  static_cast<void>(f.context().set_draw_block(bytes, bw::shaders::LayoutHash{0x1234U}));
  require_quiet(f, 1, "matching draw block");
  f.context().set_graphics_pipeline(*blockless);
  static_cast<void>(f.context().set_draw_block(bytes, bw::shaders::LayoutHash{0x1234U}));
  require_reported(f, 1, ValidationCode::DrawBlockLayoutMismatch, TransitionError::None,
                   "Blockless", "set_draw_block");
  f.context().end_render_pass();

  f.context().set_compute_pipeline(*compute);
  static_cast<void>(f.context().set_draw_block(bytes, bw::shaders::LayoutHash{0x66U}));
  require_reported(f, 2, ValidationCode::DrawBlockLayoutMismatch, TransitionError::None, "Compute",
                   "set_draw_block");
  static_cast<void>(f.context().set_draw_block(bytes, bw::shaders::LayoutHash{0x55U}));
  require_quiet(f, 3, "matching compute draw block");
}

// (command_list.hpp, CommandList::begin_marker, CommandList::end_marker:
// markers nest and must balance); (backend.hpp, wrap_with_validation: the
// marker nesting rules): a stray end and a marker open at submit_frame are
// MarkerUnbalanced; a balanced frame is not.
void marker_unbalanced_named() {
  Fixture f;
  f.context().end_marker();
  require_reported(f, 0, ValidationCode::MarkerUnbalanced, TransitionError::None, "", "end_marker");
  f.context().begin_marker("Open");
  f.backend->submit_frame(1);
  require_reported(f, 1, ValidationCode::MarkerUnbalanced, TransitionError::None, "",
                   "submit_frame");
  f.context().begin_marker("Balanced");
  f.context().end_marker();
  f.backend->submit_frame(2);
  require_quiet(f, 2, "balanced frame");
}

// (queries.hpp, QueryKind::Occlusion: occlusion begin and end bracket draws
// inside a pass whose description names the pool; a begin outside such a pass
// is rejected): QueryOutsidePass; an index at or beyond the pool's count:
// QueryIndexOutOfRange (queries.hpp, QueryPoolDescription::count).
void query_outside_pass_and_index_named() {
  Fixture f;
  const auto target = texture(f, "Target", Access::RenderTarget);
  const auto pool = bw_rhi_test::make<bw_rhi_validation_test::TestQueryPool>(
      f.queue, bw::rhi::QueryPoolDescription{bw::rhi::QueryKind::Occlusion, 2, "Occlusion"});

  f.context().begin_query(*pool, 0);
  require_reported(f, 0, ValidationCode::QueryOutsidePass, TransitionError::None, "Occlusion",
                   "begin_query");
  f.context().begin_render_pass(pass_for(*target));
  f.context().begin_query(*pool, 0);
  require_reported(f, 1, ValidationCode::QueryOutsidePass, TransitionError::None, "Occlusion",
                   "begin_query");
  f.context().end_render_pass();

  f.context().begin_render_pass(pass_for(*target, pool.get()));
  f.context().begin_query(*pool, 2);
  require_reported(f, 2, ValidationCode::QueryIndexOutOfRange, TransitionError::None, "Occlusion",
                   "begin_query");
  f.context().begin_query(*pool, 1);
  f.context().end_query(*pool, 1);
  require_quiet(f, 3, "query inside the pass that names the pool");
  f.context().end_render_pass();
}

// (transitions.hpp, validate_use: at use, a render pass target in
// RenderTarget or the depth states, a copy destination in CopyDestination, a
// vertex or index buffer in VertexOrIndexBuffer, a staging copy source in
// CopySource; AccessMissingAtUse); (backend.hpp, wrap_with_validation: every
// use checked). Each command is issued where
// (command_list.hpp, CommandList::begin_render_pass) allows it, so the access
// check is the one that fires.
void use_checks_cover_every_named_use() {
  Fixture f;
  bw::rhi::TextureDescription depth_description = bw_rhi_test::render_target_description("Depth");
  depth_description.format = bw::rhi::Format::D32Float;
  depth_description.usage = bw::rhi::TextureUsage::DepthStencil;
  depth_description.initial_access = Access::ShaderResourcePixel;
  const auto depth = bw_rhi_test::make<bw_rhi_test::TestTexture>(f.queue, depth_description);
  const auto target = texture(f, "Target", Access::RenderTarget);
  const auto vertices = buffer(f, "Vertices", Access::CopySource);
  const auto source = buffer(f, "Source", Access::CopySource);
  const auto destination = buffer(f, "Destination", Access::ShaderResourcePixel);
  const auto read_back = texture(f, "ReadBack", Access::CopyDestination);
  const auto staging = bw_rhi_test::make<bw_rhi_validation_test::TestStagingBuffer>(f.queue);

  bw::rhi::RenderPassDescription depth_pass{};
  depth_pass.depth_stencil.texture = depth.get();
  depth_pass.name = "Depth pass";
  require(bw::rhi::validate_render_pass(depth_pass) == bw::rhi::RenderPassError::None,
          "the depth-only pass is valid");
  f.context().begin_render_pass(depth_pass);
  require_reported(f, 0, ValidationCode::Transition, TransitionError::AccessMissingAtUse, "Depth",
                   "begin_render_pass");
  f.context().end_render_pass();

  f.context().begin_render_pass(pass_for(*target));
  require_quiet(f, 1, "color target in RenderTarget");
  f.context().set_vertex_buffer(0, *vertices, 0);
  require_reported(f, 1, ValidationCode::Transition, TransitionError::AccessMissingAtUse,
                   "Vertices", "set_vertex_buffer");
  f.context().set_index_buffer(*vertices, bw::rhi::IndexFormat::Uint16, 0);
  require_reported(f, 2, ValidationCode::Transition, TransitionError::AccessMissingAtUse,
                   "Vertices", "set_index_buffer");
  f.context().end_render_pass();

  f.context().copy_buffer(*source, *destination, bw::rhi::BufferCopy{0, 0, 16});
  require_reported(f, 3, ValidationCode::Transition, TransitionError::AccessMissingAtUse,
                   "Destination", "copy_buffer");
  f.context().copy_texture_to_staging(*read_back, 0, 0, *staging, 0);
  require_reported(f, 4, ValidationCode::Transition, TransitionError::AccessMissingAtUse,
                   "ReadBack", "copy_texture_to_staging");
  f.context().copy_buffer_to_staging(*destination, *staging, bw::rhi::BufferCopy{0, 0, 16});
  require_reported(f, 5, ValidationCode::Transition, TransitionError::AccessMissingAtUse,
                   "Destination", "copy_buffer_to_staging");
}

// (transitions.hpp, validate_use: a use while a transition is open on the
// resource is TransitionAlreadyOpen); (transitions.hpp, TransitionState::open:
// a transition is open from its begin until its end);
// (backend.hpp, wrap_with_validation).
void use_during_open_transition_is_named() {
  Fixture f;
  const auto source = buffer(f, "Source", Access::CopySource);
  const auto destination = buffer(f, "Destination", Access::CopyDestination);
  const std::array transitions{Transition{source.get(), Access::Unknown, Access::CopyDestination}};
  f.context().begin_transitions(TransitionBatch{transitions, {}});
  require_quiet(f, 0, "begin");
  f.context().copy_buffer(*source, *destination, bw::rhi::BufferCopy{0, 0, 16});
  require_reported(f, 0, ValidationCode::Transition, TransitionError::TransitionAlreadyOpen,
                   "Source", "copy_buffer");
}

// (transitions.hpp, validate_begin, validate_end, TransitionError::NullResource:
// validate_begin and validate_end reject a null resource first);
// (backend.hpp, wrap_with_validation: once per failing transition): the null
// entry is reported with no resource name and the valid entry of the same
// batch is not.
void null_resource_in_batch_is_named() {
  Fixture f;
  const auto valid = buffer(f, "Valid", Access::CopySource);
  const std::array transitions{Transition{nullptr, Access::Unknown, Access::CopyDestination},
                               Transition{valid.get(), Access::Unknown, Access::CopyDestination}};
  const TransitionBatch batch{transitions, {}};
  f.context().begin_transitions(batch);
  require_reported(f, 0, ValidationCode::Transition, TransitionError::NullResource, "",
                   "begin_transitions");
  f.context().end_transitions(batch);
  require_reported(f, 1, ValidationCode::Transition, TransitionError::NullResource, "",
                   "end_transitions");
}

// (command_list.hpp, CommandList::begin_render_pass: dispatch, the compute
// pipeline, transitions and the AbsoluteTime end_query only outside a pass:
// CommandInsideRenderPass; block and root constant calls legal in both
// places); (backend.hpp, wrap_with_validation).
void inside_only_and_outside_only_commands_named() {
  Fixture f;
  const auto target = texture(f, "Target", Access::RenderTarget);
  const auto moved = buffer(f, "Moved", Access::CopySource);
  const auto block = buffer(f, "Block", Access::CopySource);
  const auto graphics = graphics_pipeline(f, "Pipeline", draw_block_binding(0x10U));
  const auto compute = bw_rhi_test::make<bw_rhi_validation_test::BoundComputePipeline>(
      f.queue, draw_block_binding(0x20U), "Compute");
  const auto timestamps = bw_rhi_test::make<bw_rhi_validation_test::TestQueryPool>(
      f.queue, bw::rhi::QueryPoolDescription{bw::rhi::QueryKind::AbsoluteTime, 2, "Timestamps"});
  const std::array transitions{Transition{moved.get(), Access::Unknown, Access::CopyDestination}};
  const TransitionBatch batch{transitions, {}};
  const std::array<std::byte, 16> bytes{};
  const std::array<std::uint32_t, 2> words{1, 2};

  f.context().begin_render_pass(pass_for(*target));
  f.context().dispatch(bw::rhi::DispatchArguments{});
  require_reported(f, 0, ValidationCode::CommandInsideRenderPass, TransitionError::None, "",
                   "dispatch");
  f.context().begin_transitions(batch);
  require_reported(f, 1, ValidationCode::CommandInsideRenderPass, TransitionError::None, "",
                   "begin_transitions");
  f.context().end_transitions(batch);
  require_reported(f, 2, ValidationCode::CommandInsideRenderPass, TransitionError::None, "",
                   "end_transitions");
  f.context().set_compute_pipeline(*compute);
  require_reported(f, 3, ValidationCode::CommandInsideRenderPass, TransitionError::None, "Compute",
                   "set_compute_pipeline");
  f.context().end_query(*timestamps, 0);
  require_reported(f, 4, ValidationCode::CommandInsideRenderPass, TransitionError::None,
                   "Timestamps", "end_query");

  f.context().set_graphics_pipeline(*graphics);
  f.context().set_static_block(0, *block, 0);
  static_cast<void>(f.context().set_draw_block(bytes, bw::shaders::LayoutHash{0x10U}));
  f.context().set_root_constants(0, words);
  require_quiet(f, 5, "block and root constant calls inside a pass");
  f.context().end_render_pass();

  f.context().set_compute_pipeline(*compute);
  f.context().set_static_block(0, *block, 0);
  static_cast<void>(f.context().set_draw_block(bytes, bw::shaders::LayoutHash{0x20U}));
  f.context().set_root_constants(0, words);
  f.context().dispatch(bw::rhi::DispatchArguments{});
  f.context().end_query(*timestamps, 0);
  require_quiet(f, 5, "compute pipeline, blocks, dispatch and timestamp outside a pass");
}

// (command_list.hpp, CommandList::begin_render_pass: a pass still open at
// end_frame is a violation, RenderPassOpenAtSubmit);
// (backend.hpp, ValidationCode::RenderPassOpenAtSubmit). The failure names the
// pass even after the caller's name storage has changed; the next frame
// starts with no pass open.
void render_pass_open_at_submit_is_named() {
  Fixture f;
  const auto target = texture(f, "Target", Access::RenderTarget);
  std::string name = "Open pass";
  bw::rhi::RenderPassDescription pass = pass_for(*target);
  pass.name = name;
  f.context().begin_render_pass(pass);
  name.assign("Overwritten");
  f.backend->submit_frame(1);
  require_reported(f, 0, ValidationCode::RenderPassOpenAtSubmit, TransitionError::None, "Open pass",
                   "submit_frame");
  f.context().begin_render_pass(pass_for(*target));
  f.context().end_render_pass();
  f.backend->submit_frame(2);
  require_quiet(f, 1, "a frame whose pass ended");
}

// (queries.hpp, QueryKind::AbsoluteTime: begin_query is not recorded for an
// AbsoluteTime pool; the validation layer reports one as QueryKindMismatch);
// an end_query outside a pass is legal for that kind
// (command_list.hpp, CommandList::end_query).
void query_kind_mismatch_is_named() {
  Fixture f;
  const auto timestamps = bw_rhi_test::make<bw_rhi_validation_test::TestQueryPool>(
      f.queue, bw::rhi::QueryPoolDescription{bw::rhi::QueryKind::AbsoluteTime, 2, "Timestamps"});
  const std::size_t calls = f.context_calls();
  f.context().begin_query(*timestamps, 0);
  require_reported(f, 0, ValidationCode::QueryKindMismatch, TransitionError::None, "Timestamps",
                   "begin_query");
  require_forwarded(f, calls, "begin_query");
  f.context().end_query(*timestamps, 0);
  require_quiet(f, 1, "end_query on the timestamp pool outside a pass");
}

// Issues one command through the layer's context; requires the inner context
// to have received exactly that call, once, with `args`, and `reports` new
// failures.
template <class Command>
void forwarded(Fixture& f, std::string_view name, const std::string& args, std::size_t reports,
               Command command) {
  const std::size_t calls = f.context_calls();
  const std::size_t before = f.captured.count;
  command(f.context());
  require_forwarded(f, calls, name);
  require(f.inner->recording_context().calls.back().args == args,
          std::string("same arguments: ") + std::string(name));
  require(f.captured.count == before + reports, std::string("failures reported by ") +
                                                    std::string(name) + ": " +
                                                    std::to_string(f.captured.count - before));
}

// As forwarded, for a Backend entry.
template <class Entry>
void backend_forwarded(Fixture& f, std::string_view name, const std::string& args, Entry entry) {
  const std::size_t calls = f.inner->calls.size();
  entry(*f.backend);
  require(f.inner->calls.size() == calls + 1, std::string("forwarded once: ") + std::string(name));
  require(f.inner->calls.back().name == name && f.inner->calls.back().args == args,
          std::string("forwarded with the same arguments: ") + std::string(name));
  require_quiet(f, 0, name);
}

template <class R> void require_inner_result(const R& result, std::string_view name) {
  require(!result.ok() && result.detail() == bw_rhi_validation_test::kRejected,
          std::string("the inner's result returned: ") + std::string(name));
}

void every_backend_entry_forwarded(Fixture& f) {
  const auto source = buffer(f, "Source", Access::CopySource);
  const auto target = texture(f, "Target", Access::RenderTarget);
  const auto staging = bw_rhi_test::make<bw_rhi_validation_test::TestStagingBuffer>(f.queue);
  const auto fence = bw_rhi_test::make<bw_rhi_validation_test::TestFence>(f.queue);
  const auto pool = bw_rhi_test::make<bw_rhi_validation_test::TestQueryPool>(
      f.queue, bw::rhi::QueryPoolDescription{bw::rhi::QueryKind::Occlusion, 4, "Occlusion"});
  const std::array<std::byte, 8> bytes{};
  const std::array<bw::rhi::SubresourceData, 1> subresources{};
  const bw::rhi::DescriptorSlot slot{7, bw::shaders::DescriptorArray::Texture2D};
  const bw::rhi::BufferViewDescription buffer_view{16, 32};
  bw::rhi::TextureViewDescription texture_view{};
  texture_view.first_mip = 1;
  texture_view.mip_count = 1;
  const bw::rhi::PipelineBinding binding = draw_block_binding(0x77U);

  require(&f.backend->capabilities() == &f.inner->capabilities(),
          "capabilities are the inner's report");
  const std::uint32_t format_calls = f.inner->format_capabilities_calls;
  require(f.backend->format_capabilities(bw::rhi::Format::R16G16B16A16Float) ==
              f.inner->format_capability,
          "format_capabilities returns the inner's value");
  require(f.inner->format_capabilities_calls == format_calls + 1 &&
              f.inner->last_format == bw::rhi::Format::R16G16B16A16Float,
          "format_capabilities forwarded once with the format");

  const bw::rhi::BufferDescription buffer_description = source->description();
  backend_forwarded(f, "create_buffer",
                    describe(buffer_description) + describe(std::span<const std::byte>(bytes)),
                    [&](bw::rhi::Backend& b) {
                      require_inner_result(b.create_buffer(buffer_description, bytes), "buffer");
                    });
  const bw::rhi::TextureDescription texture_description = target->description();
  backend_forwarded(f, "create_texture",
                    describe(texture_description) +
                        describe(std::span<const bw::rhi::SubresourceData>(subresources)),
                    [&](bw::rhi::Backend& b) {
                      require_inner_result(b.create_texture(texture_description, subresources),
                                           "texture");
                    });
  backend_forwarded(f, "create_shader_resource_view(buffer)",
                    (Args{} << static_cast<const void*>(source.get())).str() +
                        describe(buffer_view) + describe(slot),
                    [&](bw::rhi::Backend& b) {
                      require_inner_result(b.create_shader_resource_view(
                                               Ref<bw::rhi::Buffer>(source), buffer_view, slot),
                                           "srv buffer");
                    });
  backend_forwarded(f, "create_shader_resource_view(texture)",
                    (Args{} << static_cast<const void*>(target.get())).str() +
                        describe(texture_view) + describe(slot),
                    [&](bw::rhi::Backend& b) {
                      require_inner_result(b.create_shader_resource_view(
                                               Ref<bw::rhi::Texture>(target), texture_view, slot),
                                           "srv texture");
                    });
  backend_forwarded(f, "create_unordered_access_view(buffer)",
                    (Args{} << static_cast<const void*>(source.get())).str() +
                        describe(buffer_view) + describe(slot),
                    [&](bw::rhi::Backend& b) {
                      require_inner_result(b.create_unordered_access_view(
                                               Ref<bw::rhi::Buffer>(source), buffer_view, slot),
                                           "uav buffer");
                    });
  backend_forwarded(f, "create_unordered_access_view(texture)",
                    (Args{} << static_cast<const void*>(target.get())).str() +
                        describe(texture_view) + describe(slot),
                    [&](bw::rhi::Backend& b) {
                      require_inner_result(b.create_unordered_access_view(
                                               Ref<bw::rhi::Texture>(target), texture_view, slot),
                                           "uav texture");
                    });
  bw::rhi::SamplerDescription sampler{};
  sampler.filter = bw::rhi::Filter::Trilinear;
  sampler.compare = bw::rhi::CompareFunction::Less;
  backend_forwarded(f, "create_sampler", describe(sampler) + describe(slot),
                    [&](bw::rhi::Backend& b) {
                      require_inner_result(b.create_sampler(sampler, slot), "sampler");
                    });
  bw::rhi::RasterizerDescription rasterizer{};
  rasterizer.cull = bw::rhi::CullMode::Clockwise;
  rasterizer.depth_bias = 2.0F;
  backend_forwarded(f, "create_rasterizer_state", describe(rasterizer), [&](bw::rhi::Backend& b) {
    require_inner_result(b.create_rasterizer_state(rasterizer), "rasterizer");
  });
  bw::rhi::DepthStencilDescription depth_stencil{};
  depth_stencil.depth_write = false;
  depth_stencil.depth_test = bw::rhi::CompareFunction::Greater;
  backend_forwarded(
      f, "create_depth_stencil_state", describe(depth_stencil), [&](bw::rhi::Backend& b) {
        require_inner_result(b.create_depth_stencil_state(depth_stencil), "depth stencil");
      });
  bw::rhi::BlendDescription blend{};
  blend.targets[0].color_source = bw::rhi::BlendFactor::SourceAlpha;
  blend.alpha_to_coverage = true;
  backend_forwarded(f, "create_blend_state", describe(blend), [&](bw::rhi::Backend& b) {
    require_inner_result(b.create_blend_state(blend), "blend");
  });
  bw::rhi::GraphicsPipelineDescription graphics{};
  graphics.targets.color_count = 1;
  graphics.targets.color_formats[0] = bw::rhi::Format::R8G8B8A8Unorm;
  graphics.debug_name = "Graphics";
  backend_forwarded(f, "create_graphics_pipeline", describe(graphics) + describe(binding),
                    [&](bw::rhi::Backend& b) {
                      require_inner_result(b.create_graphics_pipeline(graphics, binding),
                                           "graphics pipeline");
                    });
  const bw::rhi::ComputePipelineDescription compute{nullptr, "Compute"};
  backend_forwarded(f, "create_compute_pipeline", describe(compute) + describe(binding),
                    [&](bw::rhi::Backend& b) {
                      require_inner_result(b.create_compute_pipeline(compute, binding),
                                           "compute pipeline");
                    });
  backend_forwarded(
      f, "create_fence", (Args{} << std::string_view("Fence")).str(),
      [&](bw::rhi::Backend& b) { require_inner_result(b.create_fence("Fence"), "fence"); });
  backend_forwarded(f, "create_staging_buffer",
                    (Args{} << std::uint64_t{512} << std::string_view("Staging")).str(),
                    [&](bw::rhi::Backend& b) {
                      require_inner_result(b.create_staging_buffer(512, "Staging"), "staging");
                    });
  const bw::rhi::QueryPoolDescription pool_description{bw::rhi::QueryKind::Occlusion, 8, "Pool"};
  backend_forwarded(f, "create_query_pool", describe(pool_description), [&](bw::rhi::Backend& b) {
    require_inner_result(b.create_query_pool(pool_description), "query pool");
  });
  backend_forwarded(
      f, "lock_buffer",
      (Args{} << static_cast<const void*>(source.get()) << std::uint64_t{8} << std::uint64_t{64})
          .str(),
      [&](bw::rhi::Backend& b) {
        const bw::rhi::LockResult result = b.lock_buffer(*source, bw::rhi::BufferRange{8, 64});
        require(result.error() == bw::rhi::LockError::AlreadyLocked, "lock_buffer result returned");
      });
  backend_forwarded(f, "unlock_buffer", (Args{} << static_cast<const void*>(source.get())).str(),
                    [&](bw::rhi::Backend& b) {
                      require(b.unlock_buffer(*source) == bw::rhi::LockError::NotLocked,
                              "unlock_buffer result returned");
                    });
  backend_forwarded(f, "lock_staging",
                    (Args{} << static_cast<const void*>(staging.get())
                            << static_cast<const void*>(fence.get()) << bw::rhi::WaitPolicy::Block)
                        .str(),
                    [&](bw::rhi::Backend& b) {
                      const bw::rhi::StagingLockResult result =
                          b.lock_staging(*staging, *fence, bw::rhi::WaitPolicy::Block);
                      require(result.error() == bw::rhi::StagingError::NotReady,
                              "lock_staging result returned");
                    });
  backend_forwarded(f, "unlock_staging", (Args{} << static_cast<const void*>(staging.get())).str(),
                    [&](bw::rhi::Backend& b) {
                      require(b.unlock_staging(*staging) == bw::rhi::StagingError::NotLocked,
                              "unlock_staging result returned");
                    });
  backend_forwarded(
      f, "query_result",
      (Args{} << static_cast<const void*>(pool.get()) << std::uint64_t{3}
              << bw::rhi::WaitPolicy::PollOnly)
          .str(),
      [&](bw::rhi::Backend& b) {
        const bw::rhi::QueryResult result = b.query_result(*pool, 3, bw::rhi::WaitPolicy::PollOnly);
        require(result.error() == bw::rhi::QueryError::NotReady, "query_result result returned");
      });
  backend_forwarded(f, "submit_frame", (Args{} << std::uint64_t{7}).str(),
                    [](bw::rhi::Backend& b) { b.submit_frame(7); });
  const std::uint32_t completed_calls = f.inner->completed_frame_calls;
  require(f.backend->completed_frame() == f.inner->completed &&
              f.inner->completed_frame_calls == completed_calls + 1,
          "completed_frame forwarded once and returned");
  backend_forwarded(f, "wait_for_frame", (Args{} << std::uint64_t{5}).str(),
                    [](bw::rhi::Backend& b) { b.wait_for_frame(5); });
  backend_forwarded(f, "wait_idle", {}, [](bw::rhi::Backend& b) { b.wait_idle(); });
  // backend.hpp, take_gpu_fault: the inner's flag, once, then cleared.
  const std::uint32_t fault_calls = f.inner->take_gpu_fault_calls;
  f.inner->gpu_fault = true;
  require(f.backend->take_gpu_fault() && f.inner->take_gpu_fault_calls == fault_calls + 1,
          "take_gpu_fault forwarded once and its true returned");
  require(!f.backend->take_gpu_fault() && f.inner->take_gpu_fault_calls == fault_calls + 2,
          "take_gpu_fault forwarded again and the cleared flag returned");
  backend_forwarded(f, "reset_frame_resources", (Args{} << std::uint64_t{4}).str(),
                    [](bw::rhi::Backend& b) { b.reset_frame_resources(4); });
}

// (backend.hpp, wrap_with_validation: the returned backend forwards to inner;
// a failure is reported once and the command is then forwarded; the layer
// never changes what the backend sees). Every Backend entry, and every
// Context entry in a frame with no failure, reaches the inner exactly once
// with the arguments given and its result is returned; then every Context
// entry that has a failure case is issued in that case and still reaches the
// inner once, with exactly one failure. begin_marker has no failure case
// (it opens a scope) and appears in the first frame only; set_static_block
// and set_root_constants, legal in and out of a render pass
// (command_list.hpp, CommandList::begin_render_pass), fail only out of range
// (command_list.hpp, CommandList::set_static_block, CommandList::set_root_constants).
void every_call_forwarded_once_with_same_arguments() {
  Fixture f;
  every_backend_entry_forwarded(f);

  const auto source = buffer(f, "Source", Access::CopySource);
  const auto destination = buffer(f, "Destination", Access::CopyDestination);
  const auto vertices = buffer(f, "Vertices", Access::VertexOrIndexBuffer);
  const auto moved = buffer(f, "Moved", Access::ShaderResourcePixel);
  const auto target = texture(f, "Target", Access::RenderTarget);
  const auto texture_source = texture(f, "TextureSource", Access::CopySource);
  const auto texture_destination = texture(f, "TextureDestination", Access::CopyDestination);
  const auto staging = bw_rhi_test::make<bw_rhi_validation_test::TestStagingBuffer>(f.queue);
  const auto fence = bw_rhi_test::make<bw_rhi_validation_test::TestFence>(f.queue);
  const auto pool = bw_rhi_test::make<bw_rhi_validation_test::TestQueryPool>(
      f.queue, bw::rhi::QueryPoolDescription{bw::rhi::QueryKind::Occlusion, 4, "Occlusion"});
  const auto graphics = graphics_pipeline(f, "Pipeline", draw_block_binding(0x1111U));
  const auto compute = bw_rhi_test::make<bw_rhi_validation_test::BoundComputePipeline>(
      f.queue, draw_block_binding(0x2222U), "Compute");
  const std::array<std::byte, 16> bytes{};
  const std::array<std::uint32_t, 3> words{1, 2, 3};
  const bw::rhi::BufferCopy copy{4, 8, 12};
  bw::rhi::TextureCopyRegion texture_region{};
  texture_region.source_origin = bw::rhi::Offset3{1, 2, 0};
  texture_region.extent = bw::rhi::Extent3{8, 4, 1};
  const bw::rhi::BufferTextureRegion buffer_region{16, 256, 0, 0, 0};
  const bw::rhi::Viewport viewport{1.0F, 2.0F, 64.0F, 32.0F, 0.0F, 1.0F};
  const bw::rhi::ScissorRect scissor{3, 4, 50, 20};
  const bw::rhi::DrawArguments draw{3, 2, 1, 0};
  const bw::rhi::DrawIndexedArguments draw_indexed{6, 1, 2, -1, 0};
  const bw::rhi::DispatchArguments groups{4, 2, 1};
  const std::array transitions{Transition{moved.get(), Access::Unknown, Access::CopySource}};
  const TransitionBatch batch{transitions, {}};
  const bw::rhi::RenderPassDescription pass = pass_for(*target, pool.get());
  const auto pointer = [](const auto& object) {
    return (Args{} << static_cast<const void*>(&object)).str();
  };

  // A frame with no failure.
  using C = bw::rhi::Context;
  forwarded(f, "begin_transitions", describe(batch), 0, [&](C& c) { c.begin_transitions(batch); });
  forwarded(f, "end_transitions", describe(batch), 0, [&](C& c) { c.end_transitions(batch); });
  forwarded(f, "begin_marker", (Args{} << std::string_view("Frame")).str(), 0,
            [](C& c) { c.begin_marker("Frame"); });
  forwarded(f, "copy_buffer", pointer(*source) + pointer(*destination) + describe(copy), 0,
            [&](C& c) { c.copy_buffer(*source, *destination, copy); });
  forwarded(f, "copy_texture",
            pointer(*texture_source) + pointer(*texture_destination) + describe(texture_region), 0,
            [&](C& c) { c.copy_texture(*texture_source, *texture_destination, texture_region); });
  forwarded(f, "copy_buffer_to_texture",
            pointer(*source) + pointer(*texture_destination) + describe(buffer_region), 0,
            [&](C& c) { c.copy_buffer_to_texture(*source, buffer_region, *texture_destination); });
  forwarded(f, "copy_texture_to_staging",
            (Args{} << static_cast<const void*>(texture_source.get()) << std::uint64_t{0}
                    << std::uint64_t{0} << static_cast<const void*>(staging.get())
                    << std::uint64_t{64})
                .str(),
            0, [&](C& c) { c.copy_texture_to_staging(*texture_source, 0, 0, *staging, 64); });
  forwarded(f, "copy_buffer_to_staging", pointer(*source) + pointer(*staging) + describe(copy), 0,
            [&](C& c) { c.copy_buffer_to_staging(*source, *staging, copy); });
  forwarded(f, "set_compute_pipeline", pointer(*compute), 0,
            [&](C& c) { c.set_compute_pipeline(*compute); });
  f.inner->recording_context().draw_block_result = bw::rhi::ExecuteError::TransientBudgetExhausted;
  forwarded(f, "set_draw_block",
            describe(std::span<const std::byte>(bytes)) + (Args{} << std::uint64_t{0x2222U}).str(),
            0, [&](C& c) {
              require(c.set_draw_block(bytes, bw::shaders::LayoutHash{0x2222U}) ==
                          bw::rhi::ExecuteError::TransientBudgetExhausted,
                      "set_draw_block returns the inner's result");
            });
  f.inner->recording_context().draw_block_result = bw::rhi::ExecuteError::None;
  forwarded(f, "set_root_constants",
            (Args{} << std::uint64_t{2}).str() + describe(std::span<const std::uint32_t>(words)), 0,
            [&](C& c) { c.set_root_constants(2, words); });
  forwarded(
      f, "set_static_block",
      (Args{} << std::uint64_t{1} << static_cast<const void*>(source.get()) << std::uint64_t{32})
          .str(),
      0, [&](C& c) { c.set_static_block(1, *source, 32); });
  forwarded(f, "dispatch", describe(groups), 0, [&](C& c) { c.dispatch(groups); });
  forwarded(f, "write_fence", pointer(*fence), 0, [&](C& c) { c.write_fence(*fence); });
  forwarded(f, "begin_render_pass", describe(pass), 0, [&](C& c) { c.begin_render_pass(pass); });
  forwarded(f, "set_graphics_pipeline", pointer(*graphics), 0,
            [&](C& c) { c.set_graphics_pipeline(*graphics); });
  forwarded(f, "set_viewport", describe(viewport), 0, [&](C& c) { c.set_viewport(viewport); });
  forwarded(f, "set_scissor", describe(scissor), 0, [&](C& c) { c.set_scissor(scissor); });
  forwarded(f, "set_stencil_reference", (Args{} << std::uint64_t{9}).str(), 0,
            [](C& c) { c.set_stencil_reference(9); });
  forwarded(
      f, "set_vertex_buffer",
      (Args{} << std::uint64_t{2} << static_cast<const void*>(vertices.get()) << std::uint64_t{16})
          .str(),
      0, [&](C& c) { c.set_vertex_buffer(2, *vertices, 16); });
  forwarded(f, "set_index_buffer",
            (Args{} << static_cast<const void*>(vertices.get()) << bw::rhi::IndexFormat::Uint32
                    << std::uint64_t{8})
                .str(),
            0, [&](C& c) { c.set_index_buffer(*vertices, bw::rhi::IndexFormat::Uint32, 8); });
  forwarded(
      f, "set_draw_block",
      describe(std::span<const std::byte>(bytes)) + (Args{} << std::uint64_t{0x1111U}).str(), 0,
      [&](C& c) { static_cast<void>(c.set_draw_block(bytes, bw::shaders::LayoutHash{0x1111U})); });
  forwarded(f, "begin_query",
            (Args{} << static_cast<const void*>(pool.get()) << std::uint64_t{1}).str(), 0,
            [&](C& c) { c.begin_query(*pool, 1); });
  forwarded(f, "draw", describe(draw), 0, [&](C& c) { c.draw(draw); });
  forwarded(f, "draw_indexed", describe(draw_indexed), 0,
            [&](C& c) { c.draw_indexed(draw_indexed); });
  forwarded(f, "end_query",
            (Args{} << static_cast<const void*>(pool.get()) << std::uint64_t{1}).str(), 0,
            [&](C& c) { c.end_query(*pool, 1); });
  forwarded(f, "end_render_pass", {}, 0, [](C& c) { c.end_render_pass(); });
  forwarded(f, "end_marker", {}, 0, [](C& c) { c.end_marker(); });
  require_quiet(f, 0, "the frame with no failure");

  // Every entry with a failure case, in that case, on a fresh layer: no
  // pass, no pipeline, no marker open.
  Fixture g;
  const auto vertices_g = buffer(g, "Vertices", Access::VertexOrIndexBuffer);
  const auto destination_g = buffer(g, "Destination", Access::CopyDestination);
  const auto texture_g = texture(g, "TextureDestination", Access::CopyDestination);
  const auto target_g = texture(g, "Target", Access::RenderTarget);
  const auto staging_g = bw_rhi_test::make<bw_rhi_validation_test::TestStagingBuffer>(g.queue);
  const auto fence_g = bw_rhi_test::make<bw_rhi_validation_test::TestFence>(g.queue);
  const auto pool_g = bw_rhi_test::make<bw_rhi_validation_test::TestQueryPool>(
      g.queue, bw::rhi::QueryPoolDescription{bw::rhi::QueryKind::Occlusion, 4, "Occlusion"});
  const auto graphics_g = graphics_pipeline(g, "Pipeline");
  const auto compute_g = bw_rhi_test::make<bw_rhi_validation_test::BoundComputePipeline>(
      g.queue, bw::rhi::PipelineBinding{}, "Compute");
  const bw::rhi::RenderPassDescription pass_g = pass_for(*target_g);
  const std::array wrong{Transition{vertices_g.get(), Access::CopySource, Access::CopyDestination}};
  const std::array unopened{Transition{destination_g.get(), Access::Unknown, Access::CopySource}};
  const TransitionBatch wrong_batch{wrong, {}};
  const TransitionBatch unopened_batch{unopened, {}};

  forwarded(g, "end_render_pass", {}, 1, [](C& c) { c.end_render_pass(); });
  forwarded(g, "set_graphics_pipeline", pointer(*graphics_g), 1,
            [&](C& c) { c.set_graphics_pipeline(*graphics_g); });
  forwarded(g, "set_viewport", describe(viewport), 1, [&](C& c) { c.set_viewport(viewport); });
  forwarded(g, "set_scissor", describe(scissor), 1, [&](C& c) { c.set_scissor(scissor); });
  forwarded(g, "set_stencil_reference", (Args{} << std::uint64_t{9}).str(), 1,
            [](C& c) { c.set_stencil_reference(9); });
  forwarded(g, "set_vertex_buffer",
            (Args{} << std::uint64_t{0} << static_cast<const void*>(vertices_g.get())
                    << std::uint64_t{16})
                .str(),
            1, [&](C& c) { c.set_vertex_buffer(0, *vertices_g, 16); });
  forwarded(g, "set_index_buffer",
            (Args{} << static_cast<const void*>(vertices_g.get()) << bw::rhi::IndexFormat::Uint16
                    << std::uint64_t{0})
                .str(),
            1, [&](C& c) { c.set_index_buffer(*vertices_g, bw::rhi::IndexFormat::Uint16, 0); });
  forwarded(
      g, "set_draw_block",
      describe(std::span<const std::byte>(bytes)) + (Args{} << std::uint64_t{0x1111U}).str(), 1,
      [&](C& c) { static_cast<void>(c.set_draw_block(bytes, bw::shaders::LayoutHash{0x1111U})); });
  forwarded(g, "set_static_block",
            (Args{} << std::uint64_t{bw::shaders::kMaxStaticBlocks}
                    << static_cast<const void*>(vertices_g.get()) << std::uint64_t{0})
                .str(),
            1, [&](C& c) { c.set_static_block(bw::shaders::kMaxStaticBlocks, *vertices_g, 0); });
  forwarded(g, "set_root_constants",
            (Args{} << std::uint64_t{bw::shaders::kRootConstantWords - 1U}).str() +
                describe(std::span<const std::uint32_t>(words)),
            1, [&](C& c) { c.set_root_constants(bw::shaders::kRootConstantWords - 1U, words); });
  forwarded(g, "draw", describe(draw), 1, [&](C& c) { c.draw(draw); });
  forwarded(g, "draw_indexed", describe(draw_indexed), 1,
            [&](C& c) { c.draw_indexed(draw_indexed); });
  forwarded(g, "dispatch", describe(groups), 1, [&](C& c) { c.dispatch(groups); });
  forwarded(g, "copy_buffer", pointer(*vertices_g) + pointer(*destination_g) + describe(copy), 1,
            [&](C& c) { c.copy_buffer(*vertices_g, *destination_g, copy); });
  forwarded(g, "copy_texture", pointer(*texture_g) + pointer(*target_g) + describe(texture_region),
            1, [&](C& c) { c.copy_texture(*texture_g, *target_g, texture_region); });
  forwarded(g, "copy_buffer_to_texture",
            pointer(*vertices_g) + pointer(*texture_g) + describe(buffer_region), 1,
            [&](C& c) { c.copy_buffer_to_texture(*vertices_g, buffer_region, *texture_g); });
  forwarded(g, "copy_texture_to_staging",
            (Args{} << static_cast<const void*>(texture_g.get()) << std::uint64_t{0}
                    << std::uint64_t{0} << static_cast<const void*>(staging_g.get())
                    << std::uint64_t{0})
                .str(),
            1, [&](C& c) { c.copy_texture_to_staging(*texture_g, 0, 0, *staging_g, 0); });
  forwarded(g, "copy_buffer_to_staging",
            pointer(*vertices_g) + pointer(*staging_g) + describe(copy), 1,
            [&](C& c) { c.copy_buffer_to_staging(*vertices_g, *staging_g, copy); });
  forwarded(g, "begin_query",
            (Args{} << static_cast<const void*>(pool_g.get()) << std::uint64_t{1}).str(), 1,
            [&](C& c) { c.begin_query(*pool_g, 1); });
  forwarded(g, "end_query",
            (Args{} << static_cast<const void*>(pool_g.get()) << std::uint64_t{1}).str(), 1,
            [&](C& c) { c.end_query(*pool_g, 1); });
  forwarded(g, "end_marker", {}, 1, [](C& c) { c.end_marker(); });
  forwarded(g, "begin_transitions", describe(wrong_batch), 1,
            [&](C& c) { c.begin_transitions(wrong_batch); });
  forwarded(g, "end_transitions", describe(unopened_batch), 1,
            [&](C& c) { c.end_transitions(unopened_batch); });
  forwarded(g, "begin_render_pass", describe(pass_g), 0,
            [&](C& c) { c.begin_render_pass(pass_g); });
  forwarded(g, "begin_render_pass", describe(pass_g), 1,
            [&](C& c) { c.begin_render_pass(pass_g); });
  forwarded(g, "set_compute_pipeline", pointer(*compute_g), 1,
            [&](C& c) { c.set_compute_pipeline(*compute_g); });
  forwarded(g, "write_fence", pointer(*fence_g), 1, [&](C& c) { c.write_fence(*fence_g); });
  forwarded(g, "end_render_pass", {}, 0, [](C& c) { c.end_render_pass(); });
  forwarded(g, "begin_marker", (Args{} << std::string_view("Open")).str(), 0,
            [](C& c) { c.begin_marker("Open"); });
  const std::size_t submits = g.inner->calls.size();
  const std::size_t before = g.captured.count;
  g.backend->submit_frame(3);
  require(g.inner->calls.size() == submits + 1 && g.inner->calls.back().name == "submit_frame" &&
              g.inner->calls.back().args == (Args{} << std::uint64_t{3}).str(),
          "submit_frame forwarded once with an open marker");
  require(g.captured.count == before + 1, "one failure for the open marker");
}

// (command_list.hpp, CommandList::set_static_block: a static slot is below
// kMaxStaticBlocks, else StaticSlotOutOfRange);
// (backend.hpp, ValidationCode::StaticSlotOutOfRange). Named by the buffer.
void static_slot_out_of_range_is_named() {
  Fixture f;
  const auto block = buffer(f, "Block", Access::ShaderResourcePixel);
  f.context().set_static_block(bw::shaders::kMaxStaticBlocks, *block, 0);
  require_reported(f, 0, ValidationCode::StaticSlotOutOfRange, TransitionError::None, "Block",
                   "set_static_block");
  f.context().set_static_block(bw::shaders::kMaxStaticBlocks - 1U, *block, 0);
  require_quiet(f, 1, "the last static slot");
}

// (command_list.hpp, CommandList::set_root_constants: first + n <=
// kRootConstantWords, else RootConstantsOutOfRange);
// (backend.hpp, ValidationCode::RootConstantsOutOfRange).
void root_constants_out_of_range_is_named() {
  Fixture f;
  const std::array<std::uint32_t, 3> words{1, 2, 3};
  f.context().set_root_constants(bw::shaders::kRootConstantWords - 2U, words);
  require_reported(f, 0, ValidationCode::RootConstantsOutOfRange, TransitionError::None, "",
                   "set_root_constants");
  f.context().set_root_constants(bw::shaders::kRootConstantWords - 3U, words);
  require_quiet(f, 1, "root constants ending at the last word");
}

// (command_list.hpp, CommandList::set_draw_block: the draw block is bound
// until the frame ends; a draw whose pipeline uses it with none set since the
// frame began is DrawBlockNotSet);
// (backend.hpp, ValidationCode::DrawBlockNotSet). Named by the pipeline.
void draw_block_not_set_is_named() {
  Fixture f;
  const auto target = texture(f, "Target", Access::RenderTarget);
  const auto pipeline = graphics_pipeline(f, "Pipeline", draw_block_binding(0x10U));
  const std::array<std::byte, 16> bytes{};
  f.context().begin_render_pass(pass_for(*target));
  f.context().set_graphics_pipeline(*pipeline);
  f.context().draw(bw::rhi::DrawArguments{3});
  require_reported(f, 0, ValidationCode::DrawBlockNotSet, TransitionError::None, "Pipeline",
                   "draw");
  static_cast<void>(f.context().set_draw_block(bytes, bw::shaders::LayoutHash{0x10U}));
  f.context().draw(bw::rhi::DrawArguments{3});
  require_quiet(f, 1, "a draw after set_draw_block");
  f.context().end_render_pass();
  f.backend->submit_frame(1);
  f.context().begin_render_pass(pass_for(*target));
  f.context().set_graphics_pipeline(*pipeline);
  f.context().draw(bw::rhi::DrawArguments{3});
  require_reported(f, 1, ValidationCode::DrawBlockNotSet, TransitionError::None, "Pipeline",
                   "draw");
  f.context().end_render_pass();
}

// (command_list.hpp, CommandList::set_static_block: a static slot is bound
// until the frame ends; a dispatch whose pipeline's mask names a slot not set
// since the frame began is StaticSlotNotSet);
// (backend.hpp, ValidationCode::StaticSlotNotSet). Named by the pipeline.
void static_slot_not_set_is_named() {
  Fixture f;
  const auto block = buffer(f, "Block", Access::ShaderResourcePixel);
  bw::rhi::PipelineBinding binding{};
  binding.static_slot_mask = 0x04U;
  const auto compute =
      bw_rhi_test::make<bw_rhi_validation_test::BoundComputePipeline>(f.queue, binding, "Compute");
  f.context().set_compute_pipeline(*compute);
  f.context().set_static_block(0, *block, 0);
  f.context().dispatch(bw::rhi::DispatchArguments{});
  require_reported(f, 0, ValidationCode::StaticSlotNotSet, TransitionError::None, "Compute",
                   "dispatch");
  f.context().set_static_block(2, *block, 0);
  f.context().dispatch(bw::rhi::DispatchArguments{});
  require_quiet(f, 1, "a dispatch after its static slot is set");
}

constexpr std::array<bw_rhi_test::Scenario, 22> kScenarios{{
    {"undeclared_transition_rejected_by_name", undeclared_transition_rejected_by_name},
    {"redundant_transition_reported_and_forwarded", redundant_transition_reported_and_forwarded},
    {"double_begin_and_unsolicited_end_named", double_begin_and_unsolicited_end_named},
    {"split_batch_on_single_queue_is_named", split_batch_on_single_queue_is_named},
    {"use_without_access_is_named", use_without_access_is_named},
    {"render_pass_nesting_named", render_pass_nesting_named},
    {"render_pass_description_invalid_named", render_pass_description_invalid_named},
    {"pipeline_targets_mismatch_and_no_pipeline_named",
     pipeline_targets_mismatch_and_no_pipeline_named},
    {"draw_block_layout_mismatch_named", draw_block_layout_mismatch_named},
    {"marker_unbalanced_named", marker_unbalanced_named},
    {"query_outside_pass_and_index_named", query_outside_pass_and_index_named},
    {"every_call_forwarded_once_with_same_arguments",
     every_call_forwarded_once_with_same_arguments},
    {"use_checks_cover_every_named_use", use_checks_cover_every_named_use},
    {"use_during_open_transition_is_named", use_during_open_transition_is_named},
    {"null_resource_in_batch_is_named", null_resource_in_batch_is_named},
    {"inside_only_and_outside_only_commands_named", inside_only_and_outside_only_commands_named},
    {"render_pass_open_at_submit_is_named", render_pass_open_at_submit_is_named},
    {"query_kind_mismatch_is_named", query_kind_mismatch_is_named},
    {"static_slot_out_of_range_is_named", static_slot_out_of_range_is_named},
    {"root_constants_out_of_range_is_named", root_constants_out_of_range_is_named},
    {"draw_block_not_set_is_named", draw_block_not_set_is_named},
    {"static_slot_not_set_is_named", static_slot_not_set_is_named},
}};

} // namespace

int main(int argc, char** argv) { return bw_rhi_test::run_scenario(argc, argv, kScenarios); }
