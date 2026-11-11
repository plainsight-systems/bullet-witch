#include "bw/rendering/graph/schedule_policy.hpp"

#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/compiled_frame.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// Responsibility: MetalSchedulePolicy and schedule_policy_for
// (bw/rendering/graph/schedule_policy.hpp, contracts 1 to 5).
namespace bw::graph {

namespace {

[[nodiscard]] std::span<const AttachmentActions> attachments_of(const FramePlan& plan,
                                                                std::uint32_t r) noexcept {
  const RunningPass& running = plan.running[r];
  return std::span<const AttachmentActions>(plan.attachments)
      .subspan(running.first_attachment, running.attachment_count);
}

// Whether running pass r has a ComputeWrite use. The plan carries no use
// lists, so the use is read from what the plan derives from it: a
// ComputeWrite that is not a frame-only resource's first use carries a
// transition into an unordered access state (compiled_frame.hpp, contract
// 3: a write never shares a state); a frame-only resource's first use
// carries none, but its lifetime starts at r, and the only write a Raster
// pass makes outside its attachments is ComputeWrite (access.hpp, contract
// 1), so a frame-only buffer that starts at r, or a frame-only texture that
// starts at r and is not one of r's attachments, is written from compute.
[[nodiscard]] bool has_compute_write(const FramePlan& plan, std::uint32_t r) noexcept {
  const RunningPass& running = plan.running[r];
  const std::span<const ResourceTransition> transitions =
      std::span<const ResourceTransition>(plan.transitions)
          .subspan(running.first_transition, running.transition_count);
  for (const ResourceTransition& transition : transitions) {
    if (rhi::has_any(transition.after, rhi::kUnorderedAccessMask)) {
      return true;
    }
  }
  for (const Lifetime& lifetime : plan.buffer_lifetimes) {
    if (lifetime.exists() && lifetime.first == running.pass) {
      return true;
    }
  }
  const std::span<const AttachmentActions> attachments = attachments_of(plan, r);
  for (std::size_t i = 0; i < plan.texture_lifetimes.size(); ++i) {
    const Lifetime& lifetime = plan.texture_lifetimes[i];
    if (!lifetime.exists() || lifetime.first != running.pass) {
      continue;
    }
    bool attached = false;
    for (const AttachmentActions& attachment : attachments) {
      attached = attached || attachment.texture.index == i;
    }
    if (!attached) {
      return true;
    }
  }
  return false;
}

// Contract 3: `later` follows `earlier` directly in running order, both are
// Raster, their attachment sets are equal, no attachment of `later` states
// Clear (load Clear is what Clear derives, compiled_frame.hpp contract 4;
// Keep and Overwrite merge, since inside one render pass the later pass has
// no load action to spend), and neither writes from compute.
[[nodiscard]] bool merges(const FramePlan& plan, std::uint32_t earlier,
                          std::uint32_t later) noexcept {
  if (plan.running[earlier].kind != PassKind::Raster ||
      plan.running[later].kind != PassKind::Raster) {
    return false;
  }
  const std::span<const AttachmentActions> a = attachments_of(plan, earlier);
  const std::span<const AttachmentActions> b = attachments_of(plan, later);
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].texture != b[i].texture || a[i].slot != b[i].slot || a[i].depth != b[i].depth ||
        a[i].depth_read_only != b[i].depth_read_only || b[i].load == rhi::LoadAction::Clear) {
      return false;
    }
  }
  return !has_compute_write(plan, earlier) && !has_compute_write(plan, later);
}

} // namespace

// Contract 3 merges and contract 4 marks memoryless (schedule_policy.hpp). On a
// tiled GPU an attachment that is never stored stays on chip and needs no
// memory.
Schedule MetalSchedulePolicy::schedule(const FramePlan& plan,
                                       const rhi::Capabilities& /*capabilities*/) const {
  Schedule out{};
  for (std::uint32_t r = 0; r < plan.running.size(); ++r) {
    if (plan.running[r].kind != PassKind::Raster) {
      continue;
    }
    const std::span<const AttachmentActions> attachments = attachments_of(plan, r);
    if (!out.spans.empty() && out.spans.back().last_running + 1U == r && merges(plan, r - 1U, r)) {
      // Contract 2: the span's store actions are its last pass's. The
      // attachment sets are equal, so the entries line up.
      RenderPassSpan& span = out.spans.back();
      span.last_running = r;
      for (std::uint32_t i = 0; i < span.attachment_count; ++i) {
        out.attachments[span.first_attachment + i].store = attachments[i].store;
      }
      continue;
    }
    RenderPassSpan span{};
    span.first_running = r;
    span.last_running = r;
    span.first_attachment = static_cast<std::uint32_t>(out.attachments.size());
    span.attachment_count = static_cast<std::uint32_t>(attachments.size());
    for (const AttachmentActions& attachment : attachments) {
      out.attachments.push_back(SpanAttachment{attachment.texture, attachment.slot,
                                               attachment.depth, attachment.depth_read_only,
                                               attachment.load, attachment.store, false});
    }
    out.spans.push_back(span);
  }
  // Contract 4: memoryless when the frame-only texture lives and dies inside
  // the span and the span does not store it. Imported textures have no
  // lifetime.
  for (const RenderPassSpan& span : out.spans) {
    const PassIndex first = plan.running[span.first_running].pass;
    const PassIndex last = plan.running[span.last_running].pass;
    for (std::uint32_t i = 0; i < span.attachment_count; ++i) {
      SpanAttachment& attachment = out.attachments[span.first_attachment + i];
      if (attachment.texture.index >= plan.texture_lifetimes.size()) {
        continue;
      }
      const Lifetime& lifetime = plan.texture_lifetimes[attachment.texture.index];
      attachment.memoryless = lifetime.exists() && lifetime.first >= first &&
                              lifetime.last <= last &&
                              attachment.store == rhi::StoreAction::NoAction;
    }
  }
  return out;
}

const SchedulePolicy* schedule_policy_for(rhi::BackendKind kind) noexcept {
  static const MetalSchedulePolicy metal{};
  switch (kind) {
  case rhi::BackendKind::Metal:
    return &metal;
  case rhi::BackendKind::Vulkan:
  case rhi::BackendKind::Direct3D12:
    return nullptr;
  }
  return nullptr;
}

} // namespace bw::graph
