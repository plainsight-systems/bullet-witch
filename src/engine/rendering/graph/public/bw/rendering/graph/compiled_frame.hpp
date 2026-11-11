#ifndef BW_RENDERING_GRAPH_COMPILED_FRAME_HPP
#define BW_RENDERING_GRAPH_COMPILED_FRAME_HPP

#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

// Responsibility: what compile derives from a frame's declarations
// (docs/architecture/render-graph.md, "A frame through the graph",
// invariants 4, 5, 8 and 9 and the "Load and store actions" table), and
// what the schedule policy adds to it (invariant 10, schedule_policy.hpp).
// Plain data the executor reads (executor.hpp); nothing here touches the
// RHI.
//
// Contract.
//   1. Running passes (invariant 4). FramePlan::running lists, in declared
//      order, every pass that runs: a pass runs when one of its outputs is
//      read by a pass that runs, is marked as leaving the frame, or is an
//      imported resource (its owner reads it after the frame); a pass that
//      fails that test does not run, and neither does a pass that only fed
//      it. ran() is the names of the running passes in that order: the
//      report of which passes ran.
//   2. Lifetimes (invariant 5). texture_lifetimes and buffer_lifetimes have
//      one entry per resource of the frame, at its handle index; for a
//      frame-only resource it holds the first and last running pass that
//      touch it, as declared pass indices. A frame-only resource no running
//      pass touches has no lifetime (exists() is false) and is never
//      created; an imported resource's entry never exists. A culled pass
//      extends no lifetime.
//   3. Transitions (invariant 8). Each running pass carries the transitions
//      it needs before it runs, over the resources it and the passes after
//      it use: for each resource the state before is the state the previous
//      running use left it in (the entry state for an imported resource) and
//      the state after is the state this use needs (access.hpp, rhi_access).
//      Consecutive running reads of one resource share one state, the OR of
//      their accesses, and the one transition into it is carried by the
//      first of them; the reads after it carry none. A frame-only resource
//      is created in the state of its first use, so its first use carries no
//      transition for it. exit_transitions holds, for every imported
//      resource whose state after its last running use differs from its
//      exit state, the transition to the exit state; a Presented mark makes
//      the exit state Present (declaration.hpp, contract 7), and a ReadBack
//      mark makes the state after the last use CopySource, the state the
//      executor's copy leaves it in (executor.hpp, contract 1), whether or
//      not a running pass touched it.
//   4. Load and store actions (invariant 9). For every running Raster pass,
//      attachments holds one entry per attachment use with the actions the
//      table derives, in slot order with the depth attachment last:
//        load   Clear      the use states Clear
//               NoAction   the use states Overwrite
//               Load       the use states Keep and the texture holds
//                          contents (an imported texture, or a frame-only
//                          one a running pass wrote earlier)
//        store  Store      a later running pass uses the texture, or it
//                          leaves the frame (any mark), or it is imported
//               NoAction   nothing later reads it
//      A depth texture used only to test (DepthTest) loads and stores as
//      any other: Load, then Store or NoAction by the rule above.
//      depth_read_only is true for a DepthTest use.
//   5. Schedule (invariant 10). The policy's answer: spans, each a run of
//      one or more consecutive running Raster passes that form one API
//      render pass, in running order, with one SpanAttachment per attachment
//      of the span: the load action of the span's first pass, the store
//      action of its last, and memoryless when the attachment is first used
//      and last used inside the span and stored by none of its passes. A
//      pass between two spans that is not Raster has no span. Every running
//      Raster pass is in exactly one span; the policy never adds, removes or
//      reorders a pass.
//
// The cull roots are the marks of invariant 3 and the writes to imported
// resources; no pass is kept by a flag (access.hpp). No frame-only resource
// outlives the frame, so none holds a lifetime past its last running pass.
// Transitions are recorded in their combined form before the pass that needs
// them; none is begun a pass early, since the game's passes are few enough that
// it buys nothing (rhi.md, "Transitions"). A frame-only resource is created in
// its first state (executor.hpp), and sharing storage between frame-only
// resources is left to the backend (invariant 6).
// Load and store are derived from first and last use and the Contents the pass
// states: a write-only first use clears or does not care, a read-write one
// loads, and a texture nothing later reads, depth included, is not stored.
//
// Core guidelines read: I.1 (the plan is explicit data, nothing is recomputed
// at execute), Con.5 (lookups are constexpr), C.32 (handles, not pointers,
// name resources). Performance guidelines read: CDSA.26 and CACHE.3 (the
// plan is flat index arrays, one forward loop executes it), MEM.1 (the
// vectors are reused across frames by the builder's reset).
namespace bw::graph {

using PassIndex = std::uint16_t;
inline constexpr PassIndex kNoPass = 0xFFFFU;

struct Lifetime {
  PassIndex first{kNoPass};
  PassIndex last{kNoPass};
  [[nodiscard]] constexpr bool exists() const noexcept { return first != kNoPass; }
  [[nodiscard]] friend constexpr bool operator==(Lifetime, Lifetime) noexcept = default;
};

// Exactly one of texture and buffer is valid.
struct ResourceTransition {
  TextureHandle texture{};
  BufferHandle buffer{};
  rhi::Access before{rhi::Access::Unknown};
  rhi::Access after{rhi::Access::Unknown};
};

struct AttachmentActions {
  TextureHandle texture{};
  std::uint8_t slot{};
  bool depth{};
  bool depth_read_only{};
  rhi::LoadAction load{rhi::LoadAction::Load};
  rhi::StoreAction store{rhi::StoreAction::Store};
};

// One running pass: its declared index and its ranges in the flat arrays.
struct RunningPass {
  PassIndex pass{};
  PassKind kind{PassKind::Raster};
  std::uint32_t first_transition{};
  std::uint32_t transition_count{};
  std::uint32_t first_attachment{};
  std::uint32_t attachment_count{};
  rhi::Extent2 extent{};
};

struct FramePlan {
  std::vector<RunningPass> running{};
  std::vector<ResourceTransition> transitions{};
  std::vector<AttachmentActions> attachments{};
  std::vector<Lifetime> texture_lifetimes{};
  std::vector<Lifetime> buffer_lifetimes{};
  std::vector<ResourceTransition> exit_transitions{};
  std::vector<std::string_view> names{};

  // The report of invariant 4: the names of the running passes in order.
  [[nodiscard]] const std::vector<std::string_view>& ran() const noexcept { return names; }
};

struct SpanAttachment {
  TextureHandle texture{};
  std::uint8_t slot{};
  bool depth{};
  bool depth_read_only{};
  rhi::LoadAction load{rhi::LoadAction::Load};
  rhi::StoreAction store{rhi::StoreAction::Store};
  bool memoryless{};
};

// Indices into FramePlan::running, inclusive, and into Schedule::attachments.
struct RenderPassSpan {
  std::uint32_t first_running{};
  std::uint32_t last_running{};
  std::uint32_t first_attachment{};
  std::uint32_t attachment_count{};
};

struct Schedule {
  std::vector<RenderPassSpan> spans{};
  std::vector<SpanAttachment> attachments{};
};

struct CompiledFrame {
  FramePlan plan{};
  Schedule schedule{};
};

} // namespace bw::graph

#endif
