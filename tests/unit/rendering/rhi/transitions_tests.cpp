// Unit test: public contracts of bw/rendering/rhi/transitions.hpp, one
// scenario per CTest entry. Each scenario names the header declaration it asserts.
#include "bw/rendering/rhi/transitions.hpp"

#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "test_main.hpp"
#include "test_objects.hpp"

#include <array>

namespace {
using bw::rhi::Access;
using bw::rhi::Pipeline;
using bw::rhi::TrackedAccess;
using bw::rhi::Transition;
using bw::rhi::TransitionError;
using bw::rhi::TransitionPipelines;
using bw::rhi::TransitionState;
using bw_rhi_test::require;

constexpr TransitionPipelines kGraphics{};

// One resource to name in declarations; the pure functions read only its
// address.
struct Fixture {
  bw::rhi::DeferredDeleteQueue queue;
  bw::rhi::Ref<bw_rhi_test::TestBuffer> buffer = bw_rhi_test::make<bw_rhi_test::TestBuffer>(
      queue, bw_rhi_test::shader_buffer_description("Transitioned"));
  [[nodiscard]] bw::rhi::Viewable* resource() const noexcept { return buffer.get(); }
};

TransitionState tracked_as(Access access) {
  TransitionState state{};
  state.tracked = TrackedAccess{access, Pipeline::Graphics};
  return state;
}

// (transitions.hpp, resolve_before) and (transitions.hpp, validate_begin): an
// Unknown before takes the tracked access; a known before is kept; a before
// still Unknown after resolution is UnknownBefore at begin.
void unknown_before_resolves_from_tracked() {
  const Fixture f;
  const TrackedAccess tracked{Access::RenderTarget, Pipeline::Graphics};
  const Transition resolved = bw::rhi::resolve_before(
      Transition{f.resource(), Access::Unknown, Access::CopySource}, tracked);
  require(resolved.before == Access::RenderTarget, "Unknown resolves from the tracked access");
  require(resolved.after == Access::CopySource && resolved.resource == f.resource(),
          "the rest of the declaration is unchanged");
  const Transition kept = bw::rhi::resolve_before(
      Transition{f.resource(), Access::CopyDestination, Access::CopySource}, tracked);
  require(kept.before == Access::CopyDestination, "a known before is kept");
  const Transition unresolved = bw::rhi::resolve_before(
      Transition{f.resource(), Access::Unknown, Access::CopySource}, TrackedAccess{});
  require(bw::rhi::validate_begin(unresolved, kGraphics, TransitionState{}) ==
              TransitionError::UnknownBefore,
          "a before left Unknown is rejected");
}

// (access.hpp, is_valid_access) and (transitions.hpp, validate_begin): a begin is rejected
// for a null resource, an Unknown after, an after is_valid_access rejects, a
// before other than the tracked access, in that order; a legal pair passes.
void legal_and_illegal_pairs() {
  const Fixture f;
  const TransitionState state = tracked_as(Access::RenderTarget);
  require(bw::rhi::validate_begin(
              Transition{f.resource(), Access::RenderTarget, Access::ShaderResourcePixel},
              kGraphics, state) == TransitionError::None,
          "render target to shader read is legal");
  require(bw::rhi::validate_begin(Transition{f.resource(), Access::RenderTarget,
                                             Access::ShaderResourcePixel | Access::CopySource},
                                  kGraphics, state) == TransitionError::None,
          "to a set of reads is legal");
  require(bw::rhi::validate_begin(Transition{nullptr, Access::RenderTarget, Access::Unknown},
                                  kGraphics, state) == TransitionError::NullResource,
          "a null resource first");
  require(bw::rhi::validate_begin(Transition{f.resource(), Access::CopySource, Access::Unknown},
                                  kGraphics, state) == TransitionError::UnknownAfter,
          "an Unknown after before the tracked check");
  require(bw::rhi::validate_begin(Transition{f.resource(), Access::CopySource,
                                             Access::RenderTarget | Access::CopyDestination},
                                  kGraphics, state) == TransitionError::InvalidAfter,
          "two writes as the after, before the tracked check");
  require(bw::rhi::validate_begin(Transition{f.resource(), Access::RenderTarget,
                                             Access::RenderTarget | Access::ShaderResourcePixel},
                                  kGraphics, state) == TransitionError::InvalidAfter,
          "a write with a read as the after");
  require(bw::rhi::validate_begin(
              Transition{f.resource(), Access::CopySource, Access::ShaderResourcePixel}, kGraphics,
              state) == TransitionError::BeforeDoesNotMatchTracked,
          "a before other than the tracked access");
}

// (transitions.hpp, validate_begin): a begin while a transition is open on
// the resource is rejected.
void double_begin_rejected() {
  const Fixture f;
  const Transition transition{f.resource(), Access::RenderTarget, Access::ShaderResourcePixel};
  const TransitionState open =
      bw::rhi::apply_begin(transition, kGraphics, tracked_as(Access::RenderTarget));
  require(open.open, "the begin opens a transition");
  require(bw::rhi::validate_begin(transition, kGraphics, open) ==
              TransitionError::TransitionAlreadyOpen,
          "a second begin is rejected");
}

// (transitions.hpp, validate_end): an end with no open transition, and an
// end whose after or pipelines differ from the begin, are rejected.
void unsolicited_end_rejected() {
  const Fixture f;
  const Transition transition{f.resource(), Access::RenderTarget, Access::ShaderResourcePixel};
  const TransitionState closed = tracked_as(Access::RenderTarget);
  require(bw::rhi::validate_end(transition, kGraphics, closed) == TransitionError::NoOpenTransition,
          "an unsolicited end");
  const TransitionState open = bw::rhi::apply_begin(transition, kGraphics, closed);
  require(bw::rhi::validate_end(transition, kGraphics, open) == TransitionError::None,
          "the matching end");
  require(bw::rhi::validate_end(Transition{f.resource(), Access::RenderTarget, Access::CopySource},
                                kGraphics, open) == TransitionError::EndDoesNotMatchBegin,
          "an end with another after");
  require(bw::rhi::validate_end(transition,
                                TransitionPipelines{Pipeline::Graphics, Pipeline::AsyncCompute},
                                open) == TransitionError::EndDoesNotMatchBegin,
          "an end on other pipelines");
  require(bw::rhi::validate_end(Transition{nullptr, Access::RenderTarget, Access::CopySource},
                                kGraphics, open) == TransitionError::NullResource,
          "a null resource");
}

// (transitions.hpp, validate_begin) and (transitions.hpp, effective_pipelines):
// the declared source pipelines must equal the tracked pipelines; on a device
// without async compute a declared pipeline reads as Graphics, and None reads
// as Graphics.
void pipelines_must_match() {
  const Fixture f;
  const Transition transition{f.resource(), Access::RenderTarget, Access::ShaderResourcePixel};
  require(bw::rhi::validate_begin(
              transition, TransitionPipelines{Pipeline::AsyncCompute, Pipeline::Graphics},
              tracked_as(Access::RenderTarget)) == TransitionError::PipelinesDoNotMatchTracked,
          "a source other than the tracked pipelines");
  require(bw::rhi::effective_pipelines(Pipeline::AsyncCompute, false) == Pipeline::Graphics,
          "AsyncCompute reads as Graphics without async compute");
  require(bw::rhi::effective_pipelines(Pipeline::AsyncCompute, true) == Pipeline::AsyncCompute,
          "AsyncCompute is kept with async compute");
  require(bw::rhi::effective_pipelines(Pipeline::None, true) == Pipeline::Graphics,
          "None reads as Graphics");
  const TransitionPipelines collapsed{bw::rhi::effective_pipelines(Pipeline::AsyncCompute, false),
                                      Pipeline::Graphics};
  require(bw::rhi::validate_begin(transition, collapsed, tracked_as(Access::RenderTarget)) ==
              TransitionError::None,
          "the collapsed declaration matches the tracked pipelines");
}

// (transitions.hpp, apply_end): apply_end closes the transition and
// writes the begin's after and destination as the tracked access.
void end_writes_tracked_access() {
  const Fixture f;
  const Transition transition{f.resource(), Access::RenderTarget, Access::ShaderResourcePixel};
  const TransitionPipelines pipelines{Pipeline::Graphics, Pipeline::Graphics};
  const TransitionState open =
      bw::rhi::apply_begin(transition, pipelines, tracked_as(Access::RenderTarget));
  require(open.tracked.access == Access::RenderTarget,
          "the begin does not write the tracked state");
  const TransitionState closed = bw::rhi::apply_end(open);
  require(!closed.open, "the end closes the transition");
  require(closed.tracked == (TrackedAccess{Access::ShaderResourcePixel, Pipeline::Graphics}),
          "the end writes the tracked access");
  require(bw::rhi::validate_begin(
              Transition{f.resource(), Access::ShaderResourcePixel, Access::RenderTarget},
              pipelines, closed) == TransitionError::None,
          "the next begin is checked against the written state");
}

// (transitions.hpp, validate_use): at use, every required bit must be in
// the tracked access and no transition may be open.
void use_requires_access_and_no_open_transition() {
  const Fixture f;
  const TransitionState copy_source = tracked_as(Access::CopySource | Access::ShaderResourcePixel);
  require(bw::rhi::validate_use(copy_source, Access::CopySource) == TransitionError::None,
          "the required access is tracked");
  require(bw::rhi::validate_use(copy_source, Access::CopyDestination) ==
              TransitionError::AccessMissingAtUse,
          "the required access is missing");
  const TransitionState open = bw::rhi::apply_begin(
      Transition{f.resource(), Access::CopySource | Access::ShaderResourcePixel,
                 Access::CopyDestination},
      kGraphics, copy_source);
  require(bw::rhi::validate_use(open, Access::CopySource) == TransitionError::TransitionAlreadyOpen,
          "no use while a transition is open");
}

// (transitions.hpp, is_redundant): a before equal to the after is
// redundant and is not an error.
void redundant_is_not_an_error() {
  const Fixture f;
  const Transition redundant{f.resource(), Access::CopySource, Access::CopySource};
  require(bw::rhi::is_redundant(redundant), "before equal to after is redundant");
  require(
      !bw::rhi::is_redundant(Transition{f.resource(), Access::CopySource, Access::CopyDestination}),
      "a change of state is not redundant");
  require(bw::rhi::validate_begin(redundant, kGraphics, tracked_as(Access::CopySource)) ==
              TransitionError::None,
          "a redundant transition validates");
}

constexpr std::array scenarios{
    bw_rhi_test::Scenario{"unknown_before_resolves_from_tracked",
                          unknown_before_resolves_from_tracked},
    bw_rhi_test::Scenario{"legal_and_illegal_pairs", legal_and_illegal_pairs},
    bw_rhi_test::Scenario{"double_begin_rejected", double_begin_rejected},
    bw_rhi_test::Scenario{"unsolicited_end_rejected", unsolicited_end_rejected},
    bw_rhi_test::Scenario{"pipelines_must_match", pipelines_must_match},
    bw_rhi_test::Scenario{"end_writes_tracked_access", end_writes_tracked_access},
    bw_rhi_test::Scenario{"use_requires_access_and_no_open_transition",
                          use_requires_access_and_no_open_transition},
    bw_rhi_test::Scenario{"redundant_is_not_an_error", redundant_is_not_an_error},
};
} // namespace

int main(int argc, char** argv) { return bw_rhi_test::run_scenario(argc, argv, scenarios); }
