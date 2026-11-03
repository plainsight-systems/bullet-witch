#ifndef BW_RENDERING_RHI_TRANSITIONS_HPP
#define BW_RENDERING_RHI_TRANSITIONS_HPP
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include <cstdint>
#include <span>

// Responsibility: declared resource state transitions and their validation
// (docs/architecture/rhi.md, invariant 3 and "Transitions"): the caller
// declares resource, access before and access after; an Unknown before
// resolves from the tracked state; every declaration is validated in debug
// builds; a backend whose API tracks hazards may execute a transition as no
// work, but the declaration is still required. The recording calls are on
// CommandList (command_list.hpp); the pure functions here are what the
// validation layer runs and what the unit tests assert.
//
// Contract.
//   A Transition names a whole resource. resolve_before replaces an Unknown
//   before with the resource's tracked access and pipelines; after
//   resolution a before of Unknown is an error.
//   Begin and end: a transition is open from its begin until its end on the
//   same resource. validate_begin rejects, in this order: a null resource
//   (a null names a caller bug);
//   an after of Unknown; an after that is_valid_access rejects; a before
//   that differs from the tracked access; declared pipelines that differ
//   from the tracked pipelines; a begin while a transition is open on the
//   resource. validate_end rejects an end with no open transition and an end
//   whose after or pipelines differ from the begin. A before equal to the
//   after is redundant: not an error, reported by the layer as a note, and
//   executed as no work. apply_begin and apply_end advance the state the
//   next validation reads; apply_end writes the tracked access.
//   Pipelines: source and destination of a batch name the queues; on a
//   device without async compute both read as Graphics; a split batch
//   (begin on one, end on another) is rejected on such a device
//   (SourceDestinationMismatch).
//   The validation layer (backend.hpp) also asserts the tracked access at
//   use: a render pass target must be in RenderTarget or the depth states,
//   a copy source in CopySource, a copy destination in CopyDestination, a
//   vertex or index buffer in VertexOrIndexBuffer, an indirect buffer in
//   IndirectArguments, a staging copy source in CopySource. It cannot see
//   which descriptor slots a shader reads, so ShaderResource and
//   UnorderedAccess states are not asserted at draw time; that is the
//   omission of the bindless model and is stated here rather than hidden. A
//   static block's buffer is read as a uniform buffer in the shader-resource
//   state its Uniform usage was created in, and is likewise not asserted at
//   use.
//
// Core guidelines read: I.1 (before and after are explicit arguments; the
// tracked state is read, never guessed), Con.5 (every check is constexpr),
// E.27 (a named code per rejection), C.32 (the resource pointer in a
// declaration is an observer; the command list that carries it must be
// replayed in the frame it was recorded, lifetime.hpp). Performance
// guidelines read: GPU.7 (transitions are the explicit dependency edges
// between GPU work; no device-wide waits), EMB.6 (the checks are data).
namespace bw::rhi {

// A transition names a whole resource, with no subresource range: the
// game's passes transition whole targets. before defaults to Unknown.
struct Transition {
  Viewable* resource{};
  Access before{Access::Unknown};
  Access after{Access::Unknown};
};

// A batch carries no flags that tune cross-queue fencing: the device has a
// single queue.
struct TransitionPipelines {
  Pipeline source{Pipeline::Graphics};
  Pipeline destination{Pipeline::Graphics};
  [[nodiscard]] friend constexpr bool operator==(TransitionPipelines,
                                                 TransitionPipelines) noexcept = default;
};
struct TransitionBatch {
  std::span<const Transition> transitions{};
  TransitionPipelines pipelines{};
};

[[nodiscard]] constexpr Transition resolve_before(Transition transition,
                                                  TrackedAccess tracked) noexcept {
  if (transition.before == Access::Unknown) {
    transition.before = tracked.access;
  }
  return transition;
}

[[nodiscard]] constexpr bool is_redundant(const Transition& transition) noexcept {
  return transition.before == transition.after;
}

enum class TransitionError : std::uint8_t {
  None,
  NullResource,
  UnknownAfter,
  // The before is Unknown after resolution, which cannot happen for a
  // resource created with a known initial access; kept so the code is named.
  UnknownBefore,
  InvalidAfter,
  BeforeDoesNotMatchTracked,
  PipelinesDoNotMatchTracked,
  TransitionAlreadyOpen,
  NoOpenTransition,
  EndDoesNotMatchBegin,
  // A split batch on a device with one queue.
  SourceDestinationMismatch,
  // At use: the tracked access lacks the access the operation needs.
  AccessMissingAtUse,
};

// The validation layer's per-resource state: the tracked access the next
// begin is checked against, and the open transition if any.
struct TransitionState {
  TrackedAccess tracked{};
  Access pending_after{Access::Unknown};
  Pipeline pending_pipelines{Pipeline::None};
  bool open{};
};

// The pipelines a batch's source or destination reads as on this device.
[[nodiscard]] constexpr Pipeline effective_pipelines(Pipeline declared,
                                                     bool async_compute) noexcept {
  if (async_compute || declared == Pipeline::None) {
    return declared == Pipeline::None ? Pipeline::Graphics : declared;
  }
  return Pipeline::Graphics;
}

[[nodiscard]] constexpr TransitionError validate_begin(const Transition& resolved,
                                                       TransitionPipelines pipelines,
                                                       const TransitionState& state) noexcept {
  if (resolved.resource == nullptr) {
    return TransitionError::NullResource;
  }
  if (resolved.after == Access::Unknown) {
    return TransitionError::UnknownAfter;
  }
  if (resolved.before == Access::Unknown) {
    return TransitionError::UnknownBefore;
  }
  if (!is_valid_access(resolved.after)) {
    return TransitionError::InvalidAfter;
  }
  if (resolved.before != state.tracked.access) {
    return TransitionError::BeforeDoesNotMatchTracked;
  }
  if (pipelines.source != state.tracked.pipelines) {
    return TransitionError::PipelinesDoNotMatchTracked;
  }
  if (state.open) {
    return TransitionError::TransitionAlreadyOpen;
  }
  return TransitionError::None;
}

[[nodiscard]] constexpr TransitionState apply_begin(const Transition& resolved,
                                                    TransitionPipelines pipelines,
                                                    TransitionState state) noexcept {
  state.open = true;
  state.pending_after = resolved.after;
  state.pending_pipelines = pipelines.destination;
  return state;
}

[[nodiscard]] constexpr TransitionError validate_end(const Transition& resolved,
                                                     TransitionPipelines pipelines,
                                                     const TransitionState& state) noexcept {
  if (resolved.resource == nullptr) {
    return TransitionError::NullResource;
  }
  if (!state.open) {
    return TransitionError::NoOpenTransition;
  }
  if (resolved.after != state.pending_after || pipelines.destination != state.pending_pipelines) {
    return TransitionError::EndDoesNotMatchBegin;
  }
  return TransitionError::None;
}

// Closes the transition and writes the tracked access.
[[nodiscard]] constexpr TransitionState apply_end(TransitionState state) noexcept {
  state.tracked = TrackedAccess{state.pending_after, state.pending_pipelines};
  state.open = false;
  state.pending_after = Access::Unknown;
  state.pending_pipelines = Pipeline::None;
  return state;
}

// At use: every bit of `required` is in the tracked access and no
// transition is open.
[[nodiscard]] constexpr TransitionError validate_use(const TransitionState& state,
                                                     Access required) noexcept {
  if (state.open) {
    return TransitionError::TransitionAlreadyOpen;
  }
  if (!has_all(state.tracked.access, required)) {
    return TransitionError::AccessMissingAtUse;
  }
  return TransitionError::None;
}

} // namespace bw::rhi
#endif
