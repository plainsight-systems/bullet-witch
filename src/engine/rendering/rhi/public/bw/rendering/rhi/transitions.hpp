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
//   (departure: the pin's RHICreateTransition skips a null resource with no
//   report, RHIValidation.cpp:926-927; here a null names a caller bug);
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

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHITransition.h:118-226:
// FRHITransitionInfo {Resource, Type, AccessBefore, AccessAfter, Flags},
// with constructors that leave AccessBefore Unknown. Followed. Omitted: the
// subresource range (RHITransition.h:20-101; the pin's command list checks
// whole-resource only at RHICommandList.cpp:2134-2168 and the game's passes
// transition whole targets), the commit info for reserved resources
// (:103-116), the MaintainCompression and IgnoreAfterState flags
// (RHIDefinitions.h:1535-1545) and the BVH target.
struct Transition {
  Viewable* resource{};
  Access before{Access::Unknown};
  Access after{Access::Unknown};
};

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHITransition.h:355-404:
// FRHITransitionCreateInfo {SrcPipelines, DstPipelines, Flags,
// TransitionInfos, AliasingInfos}. Followed without flags or aliasing.
// Omitted: ERHITransitionCreateFlags NoFence, NoSplit, AllowDuringRenderPass,
// AllowDecayPipelines (RHIDefinitions.h:1513-1533), which tune cross-queue
// fencing the single-queue device has no use for; transient aliasing infos
// (RHITransition.h:228-353), which arrive with the render graph.
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

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHICore/Internal/RHICoreTransitions.h:11-58:
// FResourceState takes the tracked pipelines and access when AccessBefore is
// Unknown and checks neither side remains Unknown. Followed.
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

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Private/RHIValidation.cpp:2408-2542:
// FSubresourceState::BeginTransition resolves an Unknown after state from
// the tracked state (:2414-2417), rejects a before state left Unknown
// (:2443-2446, :2501-2505), a previous access or pipelines that do not match
// the tracked state (:2474, :2503, :2509), and a duplicate begin while
// transitioning (:2469);
// :2544-2604 EndTransition rejects "Unsolicited resource end transition call"
// and an end state that differs from the begin; :2606-2640 Assert rejects
// use of a resource whose current access lacks the required bits.
// Engine/Source/Runtime/RHI/Public/RHITransition.h:502-532: MarkBegin and
// MarkEnd reject a double begin or end per pipeline. Followed as the codes
// below. Omitted: the transient acquire and discard rules
// (RHIValidation.cpp:2278-2326), the fence-issued check behind NoFence
// (:2459-2466), and the AllowDecayPipelines collapse (:2476-2490).
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
