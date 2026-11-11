#ifndef BW_RENDERING_GRAPH_SCHEDULE_POLICY_HPP
#define BW_RENDERING_GRAPH_SCHEDULE_POLICY_HPP

#include "bw/rendering/graph/compiled_frame.hpp"
#include "bw/rendering/rhi/backend.hpp"

#include <string_view>

// Responsibility: the schedule policy of a platform
// (docs/architecture/render-graph.md, invariant 10 and "Schedule policy"):
// which adjacent passes become one API render pass, which textures take no
// memory, and how load and store actions are spent. One declaration, one
// policy per RHI backend; this slice ships the Metal policy.
//
// Contract.
//   1. SchedulePolicy::schedule reads the compiled plan and the capabilities
//      the backend reports and returns the Schedule of compiled_frame.hpp,
//      contract 5. It never adds, removes or reorders a pass and never
//      changes the image: every running Raster pass is in exactly one span,
//      spans are in running order, and a span's attachments are the
//      attachments of its passes.
//   2. A span's load actions are the first pass's and its store actions the
//      last pass's (compiled_frame.hpp, contract 4); the actions of the
//      passes between are not spent, since the attachment stays in the
//      render pass.
//   3. MetalSchedulePolicy merges two adjacent running Raster passes into
//      one span when: they are consecutive in running order with no Compute
//      or Copy pass between; their attachment sets are equal (the same
//      textures in the same slots, the same depth texture with the same
//      depth_read_only); no attachment use of the later pass states Clear
//      (Keep and Overwrite both merge: inside one render pass the later
//      pass has no load action to spend); and neither pass has a
//      ComputeWrite use. A span grows while
//      the next pass merges with its last. A pass that merges with neither
//      neighbour is a span of one.
//   4. MetalSchedulePolicy marks an attachment memoryless when its lifetime
//      begins and ends inside one span and the span's store action for it
//      is NoAction: the texture is written and read inside one API render
//      pass and never stored. What the executor does with the mark is
//      executor.hpp's contract 3.
//   5. schedule_policy_for(kind) is the policy of a backend kind: the Metal
//      policy for Metal; null for a backend whose policy is not in this
//      build (the Vulkan and Direct3D 12 policies arrive with their
//      backends). The returned policy lives for the process.
//
// The policy is chosen per backend, so no tiled-GPU flag gates the Metal
// policy's merge. Load and store actions are the bandwidth a pass costs on a
// tiled GPU: a pass need not split when only its actions change, and an
// attachment never stored stays on chip and needs no memory. The renderer has
// no resolve rectangle, occlusion query, subpass hint, multiview or shading
// rate image, so the slot comparison has none of them.
//
// Core guidelines read: C.121 and I.25 (the policy is a pure abstract class
// with no data), C.67 (copy suppressed), I.11 and C.32 (the policy returned
// by schedule_policy_for is a borrowed pointer to a process-lifetime object),
// E.27 (a backend without a policy is a null, named at the call, never a
// fallback policy). Performance guidelines read: GPU.6 (merging adjacent
// passes removes the per-pass start and end cost), GPU.9 (a memoryless
// attachment is the no-allocation case of a transient).
namespace bw::graph {

class SchedulePolicy {
public:
  SchedulePolicy(const SchedulePolicy&) = delete;
  SchedulePolicy& operator=(const SchedulePolicy&) = delete;
  virtual ~SchedulePolicy() = default;

  [[nodiscard]] virtual Schedule schedule(const FramePlan& plan,
                                          const rhi::Capabilities& capabilities) const = 0;
  [[nodiscard]] virtual std::string_view name() const noexcept = 0;

protected:
  SchedulePolicy() = default;
};

class MetalSchedulePolicy final : public SchedulePolicy {
public:
  MetalSchedulePolicy() = default;
  [[nodiscard]] Schedule schedule(const FramePlan& plan,
                                  const rhi::Capabilities& capabilities) const override;
  [[nodiscard]] std::string_view name() const noexcept override { return "metal"; }
};

[[nodiscard]] const SchedulePolicy* schedule_policy_for(rhi::BackendKind kind) noexcept;

} // namespace bw::graph

#endif
