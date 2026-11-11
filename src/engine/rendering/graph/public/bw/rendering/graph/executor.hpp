#ifndef BW_RENDERING_GRAPH_EXECUTOR_HPP
#define BW_RENDERING_GRAPH_EXECUTOR_HPP

#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/compiled_frame.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"

#include <cstdint>
#include <string_view>

// Responsibility: executing a compiled frame against the RHI
// (docs/architecture/render-graph.md, the sequence diagram under "A frame
// through the graph", and invariants 5, 7 and 8): acquire, transition,
// begin, record, end, give back, then the readbacks and the exit
// transitions. The one caller of the RHI in the graph.
//
// Contract.
//   1. execute records the compiled frame into `list`, which is outside a
//      render pass on entry and on return, and returns ok() when every
//      running pass was recorded. For each span of the schedule in order,
//      and for each running pass in it:
//        a. acquire: every frame-only resource whose lifetime begins at
//           this pass is created through `device` with the description
//           the frame declared, the usage derived from its accesses
//           (declaration.hpp, contract 2) and the initial access of its
//           first use, and handed to the graph (builder.hpp, contract 4).
//           A creation the device refuses ends execute with
//           FrameOnlyTextureFailed or FrameOnlyBufferFailed, the device's
//           ResourceError and the resource's debug name; what was recorded
//           before it stays in the list and the caller decides.
//        b. transitions: at the first pass of a span, the transitions of
//           every pass in the span are recorded as one batch before the
//           render pass opens, since a transition is legal only outside a
//           pass (rhi/command_list.hpp) and the policy merges only passes
//           whose writes are their shared attachments (schedule_policy.hpp,
//           contract 3), so a later pass of the span reads nothing an
//           earlier one wrote. A Compute or Copy pass records its own batch
//           before it runs. A transition whose before equals its after is
//           not recorded.
//        c. begin: at the first pass of a span, begin_render_pass with the
//           span's attachments and their load and store actions
//           (compiled_frame.hpp, contract 5), named for the first pass, and
//           the viewport and scissor set to the attachment extent with depth
//           0 to 1. A Compute or Copy pass opens no render pass.
//        d. record: the pass's record function with a RecordContext for it.
//        e. end: at the last pass of a span, end_render_pass.
//        f. give back: every frame-only resource whose lifetime ends at
//           this pass is released to the device's deferred delete queue
//           (rhi.md, "Resource lifetime"); its handle resolves to null from
//           then on. The release happens at record time, before the list
//           is executed, which is safe by the list's precondition that it
//           is executed within the frame it was recorded in
//           (rhi/command_list.hpp, Recording): the queue tags the release
//           with that frame and destroys nothing until its GPU work is done.
//      After the last running pass: for each ReadBack mark, a transition of
//      the resource to CopySource from the state its last running use left
//      it in, or from its entry state when no running pass touched it
//      (none when that state already is CopySource), the copy to the
//      staging buffer at the mark's offset (mip 0, slice 0 of a texture; the
//      whole buffer) and write_fence; then the exit transitions of the plan
//      in one batch. A frame-only resource read back is given back after
//      its copy.
//   2. Storage sharing is not the executor's (invariant 6): each frame-only
//      resource is created and released on its own, and the RHI's delete
//      queue holds it until the GPU is done. Two frame-only resources with
//      disjoint lifetimes share no storage this slice; nothing above the
//      RHI can tell when the backend begins to share it (rhi.md, "Resource
//      lifetime").
//   3. A frame-only texture the schedule marks memoryless (compiled_frame.hpp,
//      contract 5) is created with TextureUsage::Memoryless added to its
//      usage when device.capabilities().texture_limits.memoryless is true; the
//      mark's conditions guarantee the load and store actions such a texture
//      needs (rhi/resources.hpp, Memoryless). When the capability is absent
//      the texture is created like any other and the frame is unchanged.
//   4. execute does not call end_frame, present or lock a staging buffer;
//      the host executes the list and ends the frame (rhi/device.hpp).
//
// Transitions are recorded in their combined form before the pass that needs
// them; none is begun after an earlier pass (compiled_frame.hpp). Debug markers
// are the RHI's own, no overlapping unordered-access passes are declared, and
// there is one GPU.
// The graph pools no storage: the RHI creates and releases each frame-only
// resource (contract 2), and pooling is the RHI's work.
//
// Core guidelines read: I.1 (the compiled frame, the graph, the device and
// the list are arguments), E.27 (a refused creation is a named error with
// the resource's name; nothing substitutes), F.7 (references in), C.32 (the
// list holds observers of resources the frame keeps alive, rhi/command_list.hpp).
// Performance guidelines read: GPU.7 (transitions are the explicit
// dependency edges; no device wait), GPU.8 (one batch per pass, each
// naming the real before and after), MEM.4 (a frame-only resource's life is
// one frame; the RHI's frame boundary frees it).
namespace bw::graph {

enum class ExecuteError : std::uint8_t {
  None,
  FrameOnlyTextureFailed,
  FrameOnlyBufferFailed,
};

struct ExecuteFailure {
  ExecuteError error{ExecuteError::None};
  rhi::ResourceError resource_error{rhi::ResourceError::None};
  // The device's detail, or the resource's debug name when the device gave
  // none.
  std::string_view detail{};
  [[nodiscard]] constexpr bool ok() const noexcept { return error == ExecuteError::None; }
};

// Preconditions: `frame` was compiled from `graph` since its last reset;
// `list` is outside a render pass.
[[nodiscard]] ExecuteFailure execute(const CompiledFrame& frame, FrameGraph& graph,
                                     rhi::Device& device, rhi::CommandList& list);

} // namespace bw::graph

#endif
