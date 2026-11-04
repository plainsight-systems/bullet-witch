// The validation layer (backend.hpp, wrap_with_validation): a Backend that
// owns another, forwards every call to it unchanged, and checks the commands
// its context receives against transitions.hpp, pipeline_states.hpp,
// command_list.hpp and queries.hpp. Compiled into bw_rhi only when
// BW_RHI_DEBUG is 1 (src/engine/rendering/rhi/CMakeLists.txt).
//
// ValidationBackend owns the inner backend and forwards every Backend entry.
// There is no global validation flag: nothing in this RHI asks whether
// validation is on, the wrapper's presence is the state, and a global would
// be ambient state (I.1).
//
// Reporting. A failure is reported, then the command is forwarded. Every
// occurrence reaches the reporter, with no record of failures already seen,
// because the default reporter aborts at the first failure (backend.hpp,
// report_validation_failure_and_abort), so a repeat never reaches it, and a
// test reporter observes each occurrence. Rule: a command reports at most one
// failure, the first check in the order stated beside it; the transition
// commands report at most one failure for the command
// (CommandInsideRenderPass) and at most one per Transition of the batch,
// with that transition's resource name (backend.hpp); submit_frame reports
// an open render pass and open markers, each once, then each acquired back
// buffer that is not in Present access, once per back buffer.
// Every string in a ValidationFailure views storage that outlives the
// reporter call: a resource's own debug name, a pipeline's or pool's own copy
// of its name, the pass description's name during begin_render_pass, the
// layer's own copy of the open pass's name at submit_frame, or a string
// literal for the command.
//
// Transitions. A begin carries the before state on the source pipelines and
// an end the after state on the destination pipelines. Per Transition:
// resolve_before against the tracked access, validate (transitions.hpp,
// validate_begin and validate_end), report, then apply_begin or apply_end
// regardless of the outcome. A transition to the state the resource is
// already in is reported with Redundant, and the transition still opens and
// closes, so its end is not a false NoOpenTransition. A null resource is
// reported with NullResource. One queue executes in order, so there are no
// cross-pipeline fences.
//
// Uses. validate_use rejects use during a transition and use without the
// required access. A use does not change the tracked access: it changes only
// through a declared transition (resources.hpp, Viewable). transitions.hpp
// also names an indirect buffer in IndirectArguments; no Context command
// takes an indirect buffer (backend.hpp, Context), so there is no use to
// check.
//
// Per-resource state. The layer keeps a TransitionState only while a
// transition is open on the resource, keyed by the Viewable's address; a
// resource with no entry reads its own tracked access. The entry is erased
// when the transition ends, so no entry outlives a resource that ended its
// transitions. A resource destroyed while a transition on it is open leaves
// an entry the layer cannot recognize as stale; that is the caller's error
// (transitions.hpp, begin and end) and the layer cannot see it.
//
// Back buffers (viewport.hpp, invariants 2 and 3). The layer keeps the set
// of back buffers a render pass of the frame bound and the inner context
// opened: those are the ones the backend acquired. A copy from a back buffer
// outside the set is BackBufferNotAcquired; at submit_frame every back
// buffer in the set whose tracked access is not Present is
// BackBufferNotPresentable; the set is emptied at submit_frame, the
// frame's end. The set holds observers: a back buffer stays alive through
// its viewport for the frame that names it (lifetime.hpp).
//
// Threads: the context is driven by the one submission thread
// (backend.hpp); submit_frame is called on it too. The creation entries
// touch no layer state and forward directly. No exceptions are thrown; the
// state map and the pass and marker bookkeeping are the only allocations.
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/binding_structure.hpp"
#include "bw/rendering/rhi/build.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/queries.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/transitions.hpp"
#include "bw/rendering/rhi/viewport.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/root_layout.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if !BW_RHI_DEBUG
#error "validation_layer.cpp is compiled only with BW_RHI_DEBUG 1"
#endif

namespace bw::rhi {

namespace {

[[nodiscard]] std::string_view code_name(ValidationCode code) noexcept {
  switch (code) {
  case ValidationCode::None:
    return "None";
  case ValidationCode::Transition:
    return "Transition";
  case ValidationCode::Redundant:
    return "Redundant";
  case ValidationCode::RenderPassInvalid:
    return "RenderPassInvalid";
  case ValidationCode::CommandOutsideRenderPass:
    return "CommandOutsideRenderPass";
  case ValidationCode::CommandInsideRenderPass:
    return "CommandInsideRenderPass";
  case ValidationCode::NestedRenderPass:
    return "NestedRenderPass";
  case ValidationCode::EndWithoutBegin:
    return "EndWithoutBegin";
  case ValidationCode::PipelineTargetsMismatch:
    return "PipelineTargetsMismatch";
  case ValidationCode::DrawBlockLayoutMismatch:
    return "DrawBlockLayoutMismatch";
  case ValidationCode::NoPipelineBound:
    return "NoPipelineBound";
  case ValidationCode::MarkerUnbalanced:
    return "MarkerUnbalanced";
  case ValidationCode::QueryOutsidePass:
    return "QueryOutsidePass";
  case ValidationCode::QueryIndexOutOfRange:
    return "QueryIndexOutOfRange";
  case ValidationCode::RenderPassOpenAtSubmit:
    return "RenderPassOpenAtSubmit";
  case ValidationCode::QueryKindMismatch:
    return "QueryKindMismatch";
  case ValidationCode::StaticSlotOutOfRange:
    return "StaticSlotOutOfRange";
  case ValidationCode::RootConstantsOutOfRange:
    return "RootConstantsOutOfRange";
  case ValidationCode::DrawBlockNotSet:
    return "DrawBlockNotSet";
  case ValidationCode::StaticSlotNotSet:
    return "StaticSlotNotSet";
  case ValidationCode::BackBufferNotAcquired:
    return "BackBufferNotAcquired";
  case ValidationCode::BackBufferNotPresentable:
    return "BackBufferNotPresentable";
  }
  return "Unknown";
}

[[nodiscard]] std::string_view transition_error_name(TransitionError error) noexcept {
  switch (error) {
  case TransitionError::None:
    return "None";
  case TransitionError::NullResource:
    return "NullResource";
  case TransitionError::UnknownAfter:
    return "UnknownAfter";
  case TransitionError::UnknownBefore:
    return "UnknownBefore";
  case TransitionError::InvalidAfter:
    return "InvalidAfter";
  case TransitionError::BeforeDoesNotMatchTracked:
    return "BeforeDoesNotMatchTracked";
  case TransitionError::PipelinesDoNotMatchTracked:
    return "PipelinesDoNotMatchTracked";
  case TransitionError::TransitionAlreadyOpen:
    return "TransitionAlreadyOpen";
  case TransitionError::NoOpenTransition:
    return "NoOpenTransition";
  case TransitionError::EndDoesNotMatchBegin:
    return "EndDoesNotMatchBegin";
  case TransitionError::SourceDestinationMismatch:
    return "SourceDestinationMismatch";
  case TransitionError::AccessMissingAtUse:
    return "AccessMissingAtUse";
  }
  return "Unknown";
}

// The access a depth-stencil target needs at begin_render_pass: write when
// the pass writes depth (not depth_read_only) or writes stencil (the format
// has stencil and not stencil_read_only); read otherwise. transitions.hpp
// requires "the depth states" without naming which; this is the reading of
// the description's two read-only flags (pipeline_states.hpp,
// DepthStencilTarget).
[[nodiscard]] Access depth_target_access(const DepthStencilTarget& target) noexcept {
  const bool writes_depth = !target.depth_read_only;
  const bool writes_stencil =
      format_info(target.texture->description().format).has_stencil && !target.stencil_read_only;
  return writes_depth || writes_stencil ? Access::DepthStencilWrite : Access::DepthStencilRead;
}

class ValidationContext final : public Context {
public:
  ValidationContext(Context& inner, ValidationReporter reporter, void* user,
                    bool async_compute) noexcept
      : inner_(&inner), reporter_(reporter), user_(user), async_compute_(async_compute) {}

  // ---- Transitions ---------------------------------------------------------

  // Order per Transition: NullResource; SourceDestinationMismatch (declared
  // source and destination differ on a device without async compute: the
  // batch is split across queues the device does not have; reported here,
  // at the begin, once per Transition, and not again at the end); the
  // validate_begin codes; Redundant. The state advances in every case but
  // the null resource, which has no key.
  void begin_transitions(const TransitionBatch& batch) override {
    report_if_inside_pass("begin_transitions", {});
    const bool split = !async_compute_ && batch.pipelines.source != batch.pipelines.destination;
    const TransitionPipelines pipelines = effective(batch.pipelines);
    for (const Transition& transition : batch.transitions) {
      if (transition.resource == nullptr) {
        report_transition(TransitionError::NullResource, {}, "begin_transitions");
        continue;
      }
      const TransitionState state = state_of(*transition.resource);
      const Transition resolved = resolve_before(transition, state.tracked);
      const TransitionError error = validate_begin(resolved, pipelines, state);
      const std::string_view name = transition.resource->debug_name();
      if (split) {
        report_transition(TransitionError::SourceDestinationMismatch, name, "begin_transitions");
      } else if (error != TransitionError::None) {
        report_transition(error, name, "begin_transitions");
      } else if (is_redundant(resolved)) {
        report(ValidationFailure{.code = ValidationCode::Redundant,
                                 .resource_name = name,
                                 .command = "begin_transitions"});
      }
      states_.insert_or_assign(transition.resource, apply_begin(resolved, pipelines, state));
    }
    inner_->begin_transitions(batch);
  }

  // Order per Transition: NullResource; the validate_end codes. A closed
  // transition's tracked access is written to the resource after the inner
  // context has executed the end, so the backend sees the resource as it
  // was during its own end_transitions (backend.hpp: the backend writes the
  // same value at end_transitions; resources.hpp names the layer as the
  // other writer). The write keeps the layer's next check on the declared
  // state over a backend that executes the transition as no work.
  void end_transitions(const TransitionBatch& batch) override {
    report_if_inside_pass("end_transitions", {});
    const TransitionPipelines pipelines = effective(batch.pipelines);
    for (const Transition& transition : batch.transitions) {
      if (transition.resource == nullptr) {
        report_transition(TransitionError::NullResource, {}, "end_transitions");
        continue;
      }
      const auto found = states_.find(transition.resource);
      const TransitionState state =
          found != states_.end() ? found->second : state_of(*transition.resource);
      const Transition resolved = resolve_before(transition, state.tracked);
      const TransitionError error = validate_end(resolved, pipelines, state);
      if (error != TransitionError::None) {
        report_transition(error, transition.resource->debug_name(), "end_transitions");
      }
      if (found != states_.end() && state.open) {
        // Closed here; written to the resource and erased after forwarding.
        found->second = apply_end(state);
      }
    }
    inner_->end_transitions(batch);
    for (const Transition& transition : batch.transitions) {
      if (transition.resource == nullptr) {
        continue;
      }
      const auto found = states_.find(transition.resource);
      if (found != states_.end() && !found->second.open) {
        transition.resource->set_tracked_access(found->second.tracked);
        states_.erase(found);
      }
    }
  }

  // ---- Render passes -------------------------------------------------------

  // Order: NestedRenderPass; RenderPassInvalid; each color target's use
  // (RenderTarget), then the depth target's (depth_target_access). The
  // checks run, then the pass is forwarded and the inner's result returned
  // unchanged. The pass is entered when the inner opened it, valid or not:
  // an invalid pass stores no target layout (render_target_layout's
  // precondition), so PipelineTargetsMismatch is not checked inside it. A
  // pass the inner refused (a back buffer it could not acquire,
  // backend.hpp) is not entered: the replay ends there (command_list.hpp,
  // replay), so no end_render_pass follows it. The bound graphics pipeline
  // resets: "set in this pass" (command_list.hpp). The back buffers among
  // the color targets of an opened pass join the frame's acquired set.
  ExecuteError begin_render_pass(const RenderPassDescription& pass) override {
    const RenderPassError error = validate_render_pass(pass);
    if (inside_pass_) {
      report(ValidationFailure{.code = ValidationCode::NestedRenderPass,
                               .resource_name = pass.name,
                               .command = "begin_render_pass"});
    } else if (error != RenderPassError::None) {
      report(ValidationFailure{.code = ValidationCode::RenderPassInvalid,
                               .resource_name = pass.name,
                               .command = "begin_render_pass"});
    } else {
      check_targets(pass);
    }
    const ExecuteError result = inner_->begin_render_pass(pass);
    if (result != ExecuteError::None) {
      return result;
    }
    inside_pass_ = true;
    pass_name_ = DebugName(pass.name);
    layout_valid_ = error == RenderPassError::None;
    layout_ = layout_valid_ ? render_target_layout(pass) : RenderTargetLayout{};
    occlusion_pool_ = pass.occlusion_queries;
    graphics_pipeline_ = nullptr;
    note_acquired(pass);
    return result;
  }

  void end_render_pass() override {
    if (!inside_pass_) {
      report(
          ValidationFailure{.code = ValidationCode::EndWithoutBegin, .command = "end_render_pass"});
    }
    inside_pass_ = false;
    layout_valid_ = false;
    occlusion_pool_ = nullptr;
    graphics_pipeline_ = nullptr;
    inner_->end_render_pass();
  }

  // ---- Pipelines and fixed state -------------------------------------------

  // Order: CommandOutsideRenderPass; PipelineTargetsMismatch (the pass's
  // layout is valid and differs from the pipeline's). Bound for the pass
  // only when set inside it.
  void set_graphics_pipeline(GraphicsPipeline& pipeline) override {
    const std::string_view name = pipeline.description().debug_name;
    if (!inside_pass_) {
      report(ValidationFailure{.code = ValidationCode::CommandOutsideRenderPass,
                               .resource_name = name,
                               .command = "set_graphics_pipeline"});
    } else {
      if (layout_valid_ && pipeline.description().targets != layout_) {
        report(ValidationFailure{.code = ValidationCode::PipelineTargetsMismatch,
                                 .resource_name = name,
                                 .command = "set_graphics_pipeline"});
      }
      graphics_pipeline_ = &pipeline;
    }
    inner_->set_graphics_pipeline(pipeline);
  }

  // Outside-only (command_list.hpp, Render passes): CommandInsideRenderPass
  // inside a pass. The pipeline is bound either way, because the compute
  // binding outlives passes and the inner received it; it is what dispatch
  // and an outside-pass set_draw_block are checked against.
  void set_compute_pipeline(ComputePipeline& pipeline) override {
    report_if_inside_pass("set_compute_pipeline", pipeline.debug_name());
    compute_pipeline_ = &pipeline;
    inner_->set_compute_pipeline(pipeline);
  }

  void set_viewport(const ViewportRect& viewport) override {
    report_if_outside_pass("set_viewport", {});
    inner_->set_viewport(viewport);
  }

  void set_scissor(const ScissorRect& rect) override {
    report_if_outside_pass("set_scissor", {});
    inner_->set_scissor(rect);
  }

  void set_stencil_reference(std::uint32_t reference) override {
    report_if_outside_pass("set_stencil_reference", {});
    inner_->set_stencil_reference(reference);
  }

  // ---- Geometry ------------------------------------------------------------

  // Order: CommandOutsideRenderPass (command_list.hpp lists vertex and index
  // buffer calls as inside-only); the buffer's use (VertexOrIndexBuffer).
  void set_vertex_buffer(std::uint32_t slot, Buffer& buffer, std::uint64_t offset) override {
    if (!report_if_outside_pass("set_vertex_buffer", buffer.debug_name())) {
      check_use(buffer, Access::VertexOrIndexBuffer, "set_vertex_buffer");
    }
    inner_->set_vertex_buffer(slot, buffer, offset);
  }

  void set_index_buffer(Buffer& buffer, IndexFormat format, std::uint64_t offset) override {
    if (!report_if_outside_pass("set_index_buffer", buffer.debug_name())) {
      check_use(buffer, Access::VertexOrIndexBuffer, "set_index_buffer");
    }
    inner_->set_index_buffer(buffer, format, offset);
  }

  // ---- Blocks --------------------------------------------------------------

  // No placement check: block and root constant calls are legal inside and
  // outside a pass (command_list.hpp, Render passes). No use check: a static
  // block's buffer is not asserted at use (transitions.hpp, contract).
  // command_list.hpp, Blocks: a slot at or above kMaxStaticBlocks is
  // StaticSlotOutOfRange, named by the buffer; a slot in range is set until
  // the frame ends.
  void set_static_block(std::uint32_t slot, Buffer& buffer, std::uint64_t offset) override {
    if (slot >= shaders::kMaxStaticBlocks) {
      report(ValidationFailure{.code = ValidationCode::StaticSlotOutOfRange,
                               .resource_name = buffer.debug_name(),
                               .command = "set_static_block"});
    } else {
      static_slots_set_ =
          static_cast<std::uint8_t>(static_cast<unsigned>(static_slots_set_) | (1U << slot));
    }
    inner_->set_static_block(slot, buffer, offset);
  }

  // Checked against the pipeline the block binds for: inside a pass the
  // pass's graphics pipeline, outside the compute pipeline. Order:
  // NoPipelineBound (no such pipeline, so there is no hash to compare);
  // DrawBlockLayoutMismatch (the pipeline reads no draw block, so no layout
  // matches it, or its draw block hash differs). The failure names the
  // pipeline (backend.hpp). The inner's result is returned unchanged. The
  // draw block counts as set until the frame ends (command_list.hpp,
  // Blocks) whatever the inner returns: a failed set ends the replay
  // (command_list.hpp, replay), so no draw of that list follows it.
  ExecuteError set_draw_block(std::span<const std::byte> bytes,
                              shaders::LayoutHash layout) override {
    draw_block_set_ = true;
    const PipelineBinding* binding = bound_binding();
    if (binding == nullptr) {
      report(
          ValidationFailure{.code = ValidationCode::NoPipelineBound, .command = "set_draw_block"});
    } else if (!binding->uses_draw_block || binding->draw_block_hash != layout) {
      report(ValidationFailure{.code = ValidationCode::DrawBlockLayoutMismatch,
                               .resource_name = bound_name(),
                               .command = "set_draw_block"});
    }
    return inner_->set_draw_block(bytes, layout);
  }

  // command_list.hpp, Blocks: first + n above kRootConstantWords is
  // RootConstantsOutOfRange.
  void set_root_constants(std::uint32_t first_word, std::span<const std::uint32_t> words) override {
    if (first_word > shaders::kRootConstantWords ||
        words.size() > shaders::kRootConstantWords - first_word) {
      report(ValidationFailure{.code = ValidationCode::RootConstantsOutOfRange,
                               .command = "set_root_constants"});
    }
    inner_->set_root_constants(first_word, words);
  }

  // ---- Work ----------------------------------------------------------------

  // Order: CommandOutsideRenderPass; NoPipelineBound; DrawBlockNotSet;
  // StaticSlotNotSet (check_bindings).
  void draw(const DrawArguments& arguments) override {
    check_draw("draw");
    inner_->draw(arguments);
  }

  void draw_indexed(const DrawIndexedArguments& arguments) override {
    check_draw("draw_indexed");
    inner_->draw_indexed(arguments);
  }

  // Order: CommandInsideRenderPass; NoPipelineBound (no compute pipeline);
  // DrawBlockNotSet; StaticSlotNotSet (check_bindings).
  void dispatch(const DispatchArguments& arguments) override {
    if (!report_if_inside_pass("dispatch", {})) {
      if (compute_pipeline_ == nullptr) {
        report(ValidationFailure{.code = ValidationCode::NoPipelineBound, .command = "dispatch"});
      } else {
        check_bindings(compute_pipeline_->binding(), compute_pipeline_->debug_name(), "dispatch");
      }
    }
    inner_->dispatch(arguments);
  }

  // ---- Copies --------------------------------------------------------------

  // Order for every copy: CommandInsideRenderPass; BackBufferNotAcquired (a
  // back buffer source no pass of the frame bound; checked before the use,
  // so an unacquired back buffer in Present access is named as such and not
  // as a missing CopySource access); the source's use (CopySource); the
  // destination's use (CopyDestination). A staging buffer is not a Viewable
  // and carries no tracked access.
  // The signature is Context's (backend.hpp).
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void copy_buffer(Buffer& source, Buffer& destination, const BufferCopy& copy) override {
    check_copy(source, &destination, "copy_buffer");
    inner_->copy_buffer(source, destination, copy);
  }

  // The signature is Context's (backend.hpp).
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void copy_texture(Texture& source, Texture& destination,
                    const TextureCopyRegion& region) override {
    check_copy(source, &destination, "copy_texture");
    inner_->copy_texture(source, destination, region);
  }

  void copy_buffer_to_texture(Buffer& source, const BufferTextureRegion& region,
                              Texture& destination) override {
    check_copy(source, &destination, "copy_buffer_to_texture");
    inner_->copy_buffer_to_texture(source, region, destination);
  }

  void copy_texture_to_staging(Texture& source, std::uint8_t mip, std::uint16_t slice,
                               StagingBuffer& destination,
                               std::uint64_t destination_offset) override {
    check_copy(source, nullptr, "copy_texture_to_staging");
    inner_->copy_texture_to_staging(source, mip, slice, destination, destination_offset);
  }

  void copy_buffer_to_staging(Buffer& source, StagingBuffer& destination,
                              const BufferCopy& copy) override {
    check_copy(source, nullptr, "copy_buffer_to_staging");
    inner_->copy_buffer_to_staging(source, destination, copy);
  }

  // ---- Fences and queries --------------------------------------------------

  void write_fence(GpuFence& fence) override {
    report_if_inside_pass("write_fence", fence.debug_name());
    inner_->write_fence(fence);
  }

  // AbsoluteTime (queries.hpp): QueryKindMismatch, first, because no begin
  // is legal on such a pool whatever its index. Occlusion: order
  // QueryIndexOutOfRange; QueryOutsidePass (not inside a pass whose
  // description names this pool).
  void begin_query(QueryPool& pool, std::uint32_t index) override {
    if (pool.kind() == QueryKind::AbsoluteTime) {
      report(ValidationFailure{.code = ValidationCode::QueryKindMismatch,
                               .resource_name = pool.debug_name(),
                               .command = "begin_query"});
    } else if (!report_if_index_out_of_range(pool, index, "begin_query")) {
      report_if_not_in_pool_pass(pool, "begin_query");
    }
    inner_->begin_query(pool, index);
  }

  // Occlusion as begin_query. AbsoluteTime: order QueryIndexOutOfRange;
  // CommandInsideRenderPass (command_list.hpp: the AbsoluteTime end_query
  // only outside a pass).
  void end_query(QueryPool& pool, std::uint32_t index) override {
    if (!report_if_index_out_of_range(pool, index, "end_query")) {
      if (pool.kind() == QueryKind::Occlusion) {
        report_if_not_in_pool_pass(pool, "end_query");
      } else {
        report_if_inside_pass("end_query", pool.debug_name());
      }
    }
    inner_->end_query(pool, index);
  }

  // ---- Markers -------------------------------------------------------------

  void begin_marker(std::string_view name) override {
    ++marker_depth_;
    inner_->begin_marker(name);
  }

  void end_marker() override {
    if (marker_depth_ == 0U) {
      report(ValidationFailure{.code = ValidationCode::MarkerUnbalanced, .command = "end_marker"});
    } else {
      --marker_depth_;
    }
    inner_->end_marker();
  }

  // At submit_frame (command_list.hpp): a render pass still open is
  // RenderPassOpenAtSubmit, named by the layer's copy of the pass name (the
  // description's own view may not outlive the list that carried it); open
  // markers are MarkerUnbalanced. Each is reported once and the pass and
  // marker state start over for the next frame, which a pass cannot span.
  void check_frame_end() {
    if (inside_pass_) {
      report(ValidationFailure{.code = ValidationCode::RenderPassOpenAtSubmit,
                               .resource_name = pass_name_.view(),
                               .command = "submit_frame"});
      inside_pass_ = false;
      layout_valid_ = false;
      occlusion_pool_ = nullptr;
      graphics_pipeline_ = nullptr;
    }
    if (marker_depth_ != 0U) {
      report(
          ValidationFailure{.code = ValidationCode::MarkerUnbalanced, .command = "submit_frame"});
      marker_depth_ = 0U;
    }
    // viewport.hpp, invariant 3: the caller declared Present before the
    // boundary. Reported per back buffer; the backend presents it all the
    // same.
    for (const Texture* back_buffer : acquired_back_buffers_) {
      if (state_of(*back_buffer).tracked.access != Access::Present) {
        report(ValidationFailure{.code = ValidationCode::BackBufferNotPresentable,
                                 .resource_name = back_buffer->debug_name(),
                                 .command = "submit_frame"});
      }
    }
    acquired_back_buffers_.clear();
    // Bindings do not survive end_frame (command_list.hpp, Blocks).
    draw_block_set_ = false;
    static_slots_set_ = 0U;
  }

private:
  void report(const ValidationFailure& failure) const noexcept { reporter_(failure, user_); }

  void report_transition(TransitionError error, std::string_view name,
                         std::string_view command) const noexcept {
    report(ValidationFailure{.code = ValidationCode::Transition,
                             .transition = error,
                             .resource_name = name,
                             .command = command});
  }

  // Returns true when it reported.
  bool report_if_inside_pass(std::string_view command, std::string_view name) const noexcept {
    if (inside_pass_) {
      report(ValidationFailure{.code = ValidationCode::CommandInsideRenderPass,
                               .resource_name = name,
                               .command = command});
      return true;
    }
    return false;
  }

  bool report_if_outside_pass(std::string_view command, std::string_view name) const noexcept {
    if (!inside_pass_) {
      report(ValidationFailure{.code = ValidationCode::CommandOutsideRenderPass,
                               .resource_name = name,
                               .command = command});
      return true;
    }
    return false;
  }

  bool report_if_index_out_of_range(const QueryPool& pool, std::uint32_t index,
                                    std::string_view command) const noexcept {
    if (index >= pool.count()) {
      report(ValidationFailure{.code = ValidationCode::QueryIndexOutOfRange,
                               .resource_name = pool.debug_name(),
                               .command = command});
      return true;
    }
    return false;
  }

  void report_if_not_in_pool_pass(const QueryPool& pool, std::string_view command) const noexcept {
    if (!inside_pass_ || occlusion_pool_ != &pool) {
      report(ValidationFailure{.code = ValidationCode::QueryOutsidePass,
                               .resource_name = pool.debug_name(),
                               .command = command});
    }
  }

  // The state the next check of `resource` reads: its open entry, or its
  // own tracked access with no transition open.
  [[nodiscard]] TransitionState state_of(const Viewable& resource) const {
    const auto found = states_.find(&resource);
    if (found != states_.end()) {
      return found->second;
    }
    return TransitionState{.tracked = resource.tracked_access()};
  }

  [[nodiscard]] TransitionPipelines effective(TransitionPipelines declared) const noexcept {
    return TransitionPipelines{effective_pipelines(declared.source, async_compute_),
                               effective_pipelines(declared.destination, async_compute_)};
  }

  // Returns true when it reported.
  bool check_use(const Viewable& resource, Access required, std::string_view command) const {
    const TransitionError error = validate_use(state_of(resource), required);
    if (error != TransitionError::None) {
      report_transition(error, resource.debug_name(), command);
      return true;
    }
    return false;
  }

  void check_targets(const RenderPassDescription& pass) const {
    for (std::uint32_t i = 0; i < pass.color_count; ++i) {
      if (check_use(*pass.colors[i].texture, Access::RenderTarget, "begin_render_pass")) {
        return;
      }
    }
    if (pass.depth_stencil.texture != nullptr) {
      static_cast<void>(check_use(*pass.depth_stencil.texture,
                                  depth_target_access(pass.depth_stencil), "begin_render_pass"));
    }
  }

  void check_copy(const Viewable& source, const Viewable* destination,
                  std::string_view command) const {
    if (report_if_inside_pass(command, source.debug_name())) {
      return;
    }
    if (is_back_buffer(source) && !acquired(static_cast<const Texture&>(source))) {
      report(ValidationFailure{.code = ValidationCode::BackBufferNotAcquired,
                               .resource_name = source.debug_name(),
                               .command = command});
      return;
    }
    if (check_use(source, Access::CopySource, command)) {
      return;
    }
    if (destination != nullptr) {
      static_cast<void>(check_use(*destination, Access::CopyDestination, command));
    }
  }

  void check_draw(std::string_view command) const noexcept {
    if (report_if_outside_pass(command, {})) {
      return;
    }
    if (graphics_pipeline_ == nullptr) {
      report(ValidationFailure{.code = ValidationCode::NoPipelineBound, .command = command});
      return;
    }
    check_bindings(graphics_pipeline_->binding(), graphics_pipeline_->description().debug_name,
                   command);
  }

  // command_list.hpp, Blocks: a pipeline that reads the draw block with none
  // set since the frame began is DrawBlockNotSet; one whose static slot mask
  // names a slot not set since then is StaticSlotNotSet. Each names the
  // pipeline; one failure per command, the draw block first.
  void check_bindings(const PipelineBinding& binding, std::string_view name,
                      std::string_view command) const noexcept {
    if (binding.uses_draw_block && !draw_block_set_) {
      report(ValidationFailure{
          .code = ValidationCode::DrawBlockNotSet, .resource_name = name, .command = command});
      return;
    }
    const unsigned missing = static_cast<unsigned>(binding.static_slot_mask) &
                             ~static_cast<unsigned>(static_slots_set_) & 0xFFU;
    if (missing != 0U) {
      report(ValidationFailure{
          .code = ValidationCode::StaticSlotNotSet, .resource_name = name, .command = command});
    }
  }

  [[nodiscard]] static bool is_back_buffer(const Viewable& resource) noexcept {
    return resource.is_texture() &&
           has_usage(static_cast<const Texture&>(resource).description().usage,
                     TextureUsage::BackBuffer);
  }

  [[nodiscard]] bool acquired(const Texture& back_buffer) const noexcept {
    return std::find(acquired_back_buffers_.begin(), acquired_back_buffers_.end(), &back_buffer) !=
           acquired_back_buffers_.end();
  }

  // The back buffers among an opened pass's color targets join the frame's
  // acquired set, once each.
  void note_acquired(const RenderPassDescription& pass) {
    for (std::uint32_t i = 0; i < pass.color_count; ++i) {
      const Texture* texture = pass.colors[i].texture;
      if (texture != nullptr && is_back_buffer(*texture) && !acquired(*texture)) {
        acquired_back_buffers_.push_back(texture);
      }
    }
  }

  [[nodiscard]] const PipelineBinding* bound_binding() const noexcept {
    if (inside_pass_) {
      return graphics_pipeline_ != nullptr ? &graphics_pipeline_->binding() : nullptr;
    }
    return compute_pipeline_ != nullptr ? &compute_pipeline_->binding() : nullptr;
  }

  // The bound pipeline's name (backend.hpp: the graphics description's
  // debug name, ComputePipeline::debug_name). Precondition: bound_binding()
  // is not null.
  [[nodiscard]] std::string_view bound_name() const noexcept {
    if (inside_pass_) {
      return graphics_pipeline_->description().debug_name;
    }
    return compute_pipeline_->debug_name();
  }

  Context* inner_;
  ValidationReporter reporter_;
  void* user_;
  bool async_compute_;
  std::unordered_map<const Viewable*, TransitionState> states_{};
  bool inside_pass_{};
  // A bounded copy (resources.hpp, DebugName), so no allocation.
  DebugName pass_name_{};
  bool layout_valid_{};
  RenderTargetLayout layout_{};
  const QueryPool* occlusion_pool_{};
  // Observers: the device's cache and the frame keep pipelines alive while
  // a list that names them executes (command_list.hpp, Recording).
  const GraphicsPipeline* graphics_pipeline_{};
  const ComputePipeline* compute_pipeline_{};
  std::uint32_t marker_depth_{};
  // Blocks set since the frame began (command_list.hpp, Blocks): the draw
  // block, and one bit per static slot.
  bool draw_block_set_{};
  std::uint8_t static_slots_set_{};
  // The back buffers acquired in the frame (observers; see the file
  // comment). A frame binds one back buffer per viewport, so the set is a
  // short vector searched linearly.
  std::vector<const Texture*> acquired_back_buffers_{};
};

class ValidationBackend final : public Backend {
public:
  // Capabilities are filled at creation and never change (backend.hpp), so
  // the async compute flag is read once.
  ValidationBackend(std::unique_ptr<Backend> inner, ValidationReporter reporter,
                    void* user) noexcept
      : inner_(std::move(inner)),
        context_(inner_->context(), reporter, user, inner_->capabilities().async_compute) {}

  [[nodiscard]] const Capabilities& capabilities() const noexcept override {
    return inner_->capabilities();
  }
  [[nodiscard]] FormatCapability format_capabilities(Format format) const noexcept override {
    return inner_->format_capabilities(format);
  }

  BufferResult create_buffer(const BufferDescription& description,
                             std::span<const std::byte> initial_data) override {
    return inner_->create_buffer(description, initial_data);
  }
  TextureResult create_texture(const TextureDescription& description,
                               std::span<const SubresourceData> initial_data) override {
    return inner_->create_texture(description, initial_data);
  }
  ShaderResourceViewResult create_shader_resource_view(Ref<Buffer> buffer,
                                                       const BufferViewDescription& view,
                                                       DescriptorSlot slot) override {
    return inner_->create_shader_resource_view(std::move(buffer), view, slot);
  }
  ShaderResourceViewResult create_shader_resource_view(Ref<Texture> texture,
                                                       const TextureViewDescription& view,
                                                       DescriptorSlot slot) override {
    return inner_->create_shader_resource_view(std::move(texture), view, slot);
  }
  UnorderedAccessViewResult create_unordered_access_view(Ref<Buffer> buffer,
                                                         const BufferViewDescription& view,
                                                         DescriptorSlot slot) override {
    return inner_->create_unordered_access_view(std::move(buffer), view, slot);
  }
  UnorderedAccessViewResult create_unordered_access_view(Ref<Texture> texture,
                                                         const TextureViewDescription& view,
                                                         DescriptorSlot slot) override {
    return inner_->create_unordered_access_view(std::move(texture), view, slot);
  }
  SamplerResult create_sampler(const SamplerDescription& description,
                               DescriptorSlot slot) override {
    return inner_->create_sampler(description, slot);
  }
  RasterizerStateResult create_rasterizer_state(const RasterizerDescription& description) override {
    return inner_->create_rasterizer_state(description);
  }
  DepthStencilStateResult
  create_depth_stencil_state(const DepthStencilDescription& description) override {
    return inner_->create_depth_stencil_state(description);
  }
  BlendStateResult create_blend_state(const BlendDescription& description) override {
    return inner_->create_blend_state(description);
  }
  GraphicsPipelineResult create_graphics_pipeline(const GraphicsPipelineDescription& description,
                                                  const PipelineBinding& binding) override {
    return inner_->create_graphics_pipeline(description, binding);
  }
  ComputePipelineResult create_compute_pipeline(const ComputePipelineDescription& description,
                                                const PipelineBinding& binding) override {
    return inner_->create_compute_pipeline(description, binding);
  }
  GpuFenceResult create_fence(std::string_view debug_name) override {
    return inner_->create_fence(debug_name);
  }
  StagingBufferResult create_staging_buffer(std::uint64_t size,
                                            std::string_view debug_name) override {
    return inner_->create_staging_buffer(size, debug_name);
  }
  QueryPoolResult create_query_pool(const QueryPoolDescription& description) override {
    return inner_->create_query_pool(description);
  }
  // The device validated the description and the resize (backend.hpp,
  // Viewports); the layer adds no check.
  ViewportResult create_viewport(const ViewportDescription& description) override {
    return inner_->create_viewport(description);
  }
  Status<ViewportError> resize_viewport(Viewport& viewport, Extent2 pixel_size) override {
    return inner_->resize_viewport(viewport, pixel_size);
  }
  void set_vsync(Viewport& viewport, bool vsync) override { inner_->set_vsync(viewport, vsync); }

  LockResult lock_buffer(Buffer& buffer, BufferRange range) override {
    return inner_->lock_buffer(buffer, range);
  }
  LockError unlock_buffer(Buffer& buffer) override { return inner_->unlock_buffer(buffer); }
  StagingLockResult lock_staging(StagingBuffer& staging, GpuFence& fence,
                                 WaitPolicy policy) override {
    return inner_->lock_staging(staging, fence, policy);
  }
  StagingError unlock_staging(StagingBuffer& staging) override {
    return inner_->unlock_staging(staging);
  }
  QueryResult query_result(QueryPool& pool, std::uint32_t index, WaitPolicy policy) override {
    return inner_->query_result(pool, index, policy);
  }

  [[nodiscard]] Context& context() noexcept override { return context_; }
  void submit_frame(std::uint64_t frame) override {
    context_.check_frame_end();
    inner_->submit_frame(frame);
  }
  [[nodiscard]] std::uint64_t completed_frame() const noexcept override {
    return inner_->completed_frame();
  }
  void wait_for_frame(std::uint64_t frame) override { inner_->wait_for_frame(frame); }
  void wait_idle() override { inner_->wait_idle(); }
  [[nodiscard]] bool take_gpu_fault() noexcept override { return inner_->take_gpu_fault(); }
  void reset_frame_resources(std::uint64_t completed_frame) override {
    inner_->reset_frame_resources(completed_frame);
  }

private:
  std::unique_ptr<Backend> inner_;
  ValidationContext context_;
};

void write_text(std::string_view text) noexcept {
  static_cast<void>(std::fwrite(text.data(), 1, text.size(), stderr));
}

} // namespace

// Preconditions: inner and reporter are non-null; the device passes the
// backend it just created and substitutes the default reporter for a null
// one (device.cpp, Device::create).
std::unique_ptr<Backend> wrap_with_validation(std::unique_ptr<Backend> inner,
                                              ValidationReporter reporter, void* user) {
  return std::make_unique<ValidationBackend>(std::move(inner), reporter, user);
}

// One line on stderr, then abort: "bw::rhi validation: <code>[
// (<transition error>)] resource '<name>' command '<command>'".
void report_validation_failure_and_abort(const ValidationFailure& failure, void* user) noexcept {
  static_cast<void>(user);
  write_text("bw::rhi validation: ");
  write_text(code_name(failure.code));
  if (failure.code == ValidationCode::Transition) {
    write_text(" (");
    write_text(transition_error_name(failure.transition));
    write_text(")");
  }
  write_text(" resource '");
  write_text(failure.resource_name);
  write_text("' command '");
  write_text(failure.command);
  write_text("'\n");
  static_cast<void>(std::fflush(stderr));
  std::abort();
}

} // namespace bw::rhi
