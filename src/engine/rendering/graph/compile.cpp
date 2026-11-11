#include "bw/rendering/graph/builder.hpp"

#include "graph_state.hpp"

#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/compiled_frame.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/graph/schedule_policy.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Responsibility: FrameGraph::compile (bw/rendering/graph/builder.hpp,
// contract 2): the running passes, the lifetimes, the transitions and exit
// transitions, the attachment actions (compiled_frame.hpp, contracts 1 to
// 4), then the schedule from the policy (contract 5).
namespace bw::graph {

namespace {

[[nodiscard]] bool states_contents(Access access) noexcept {
  return access == Access::Attachment || access == Access::DepthWrite;
}

// A use whose pass depends on what earlier passes left in the resource: a
// read, or an attachment write that keeps the contents (Load).
[[nodiscard]] bool depends_on_contents(Access access, Contents contents) noexcept {
  return is_read(access) || (states_contents(access) && contents == Contents::Keep);
}

[[nodiscard]] bool marked(const TextureRecord& record, const ReadBack& readback) noexcept {
  return record.presented || record.history || readback.staging != nullptr;
}

[[nodiscard]] bool marked(const BufferRecord& record, const ReadBack& readback) noexcept {
  return record.history || readback.staging != nullptr;
}

[[nodiscard]] std::string_view name_of(const TextureRecord& record) noexcept {
  if (!record.imported) {
    return record.description.debug_name;
  }
  return record.import.texture != nullptr ? record.import.texture->debug_name()
                                          : std::string_view{};
}

[[nodiscard]] std::string_view name_of(const BufferRecord& record) noexcept {
  if (!record.imported) {
    return record.description.debug_name;
  }
  return record.import.buffer != nullptr ? record.import.buffer->debug_name() : std::string_view{};
}

// The state a read-back resource is copied from: its state after its last
// running use when that already includes CopySource, else CopySource
// (executor.hpp, contract 1: "unless it is in it").
[[nodiscard]] rhi::Access after_readback(rhi::Access state) noexcept {
  return rhi::has_all(state, rhi::Access::CopySource) ? state : rhi::Access::CopySource;
}

// One transition placed at a running pass, before the plan's flat array is
// built in running order.
struct PlacedTransition {
  std::uint32_t running{};
  ResourceTransition transition{};
};

// The transitions of one resource over the running passes
// (compiled_frame.hpp, contract 3): every use opens a new state except a
// read that follows a read, which joins the open state and ORs its access
// into it; the transition into a state is carried by the use that opens it,
// and the first use of a frame-only resource carries none. Returns the
// state after the last running use; `start` is the entry state of an
// imported resource and Unknown for a frame-only one.
template <class Uses, class Make>
rhi::Access place_transitions(std::span<const RunningPass> running, rhi::Access start, Uses uses,
                              Make make, std::vector<PlacedTransition>& placed) {
  rhi::Access current = start;
  bool started = start != rhi::Access::Unknown;
  bool in_read_run = false;
  std::size_t head = placed.size();
  bool head_placed = false;
  for (std::uint32_t r = 0; r < running.size(); ++r) {
    Access access{};
    if (!uses(running[r].pass, access)) {
      continue;
    }
    const rhi::Access need = rhi_access(access, running[r].kind);
    if (!started) {
      started = true;
      current = need;
      in_read_run = is_read(access);
      head_placed = false;
      continue;
    }
    if (in_read_run && is_read(access)) {
      current = current | need;
      if (head_placed) {
        placed[head].transition.after = current;
      }
      continue;
    }
    head = placed.size();
    head_placed = true;
    placed.push_back(PlacedTransition{r, make(current, need)});
    current = need;
    in_read_run = is_read(access);
  }
  return current;
}

} // namespace

// The cull roots are the marks of invariant 3 and the writes to imported
// resources, and a reader keeps every earlier writer of what it reads; no pass
// is kept by a flag. Transitions are recorded in their combined form, and a
// frame-only resource's first use carries none because the executor creates it
// in its first state (executor.hpp). Load and store: clear or no action for a
// write-only first use, load for read-write, no store when nothing later reads
// the texture, depth included.
CompileResult FrameGraph::compile(const SchedulePolicy& policy,
                                  const rhi::Capabilities& capabilities, CompiledFrame& out) {
  const State& s = *state_;
  FramePlan& plan = out.plan;
  plan.running.clear();
  plan.transitions.clear();
  plan.attachments.clear();
  plan.texture_lifetimes.clear();
  plan.buffer_lifetimes.clear();
  plan.exit_transitions.clear();
  plan.names.clear();
  out.schedule.spans.clear();
  out.schedule.attachments.clear();
  if (!s.first_error.ok()) {
    return s.first_error;
  }
  const std::size_t pass_count = s.passes.size();
  const std::size_t texture_count = s.textures.size();
  const std::size_t buffer_count = s.buffers.size();

  // 1. Running passes (compiled_frame.hpp, contract 1): a reverse walk from
  // the resources that leave the frame. A pass runs when it writes a
  // resource a later running pass depends on, a marked one, or an imported
  // one, whose owner holds it past the frame.
  std::vector<bool> texture_needed(texture_count);
  std::vector<bool> buffer_needed(buffer_count);
  for (std::size_t i = 0; i < texture_count; ++i) {
    texture_needed[i] = s.textures[i].imported || marked(s.textures[i], s.texture_readbacks[i]);
  }
  for (std::size_t i = 0; i < buffer_count; ++i) {
    buffer_needed[i] = s.buffers[i].imported || marked(s.buffers[i], s.buffer_readbacks[i]);
  }
  std::vector<bool> runs(pass_count);
  for (std::size_t p = pass_count; p-- > 0U;) {
    const PassDeclaration& pass = s.passes[p];
    bool live = false;
    for (const TextureUse& use : pass.textures) {
      live = live || (is_write(use.access) && texture_needed[use.texture.index]);
    }
    for (const BufferUse& use : pass.buffers) {
      live = live || (is_write(use.access) && buffer_needed[use.buffer.index]);
    }
    if (!live) {
      continue;
    }
    runs[p] = true;
    for (const TextureUse& use : pass.textures) {
      if (depends_on_contents(use.access, use.contents)) {
        texture_needed[use.texture.index] = true;
      }
    }
    for (const BufferUse& use : pass.buffers) {
      if (is_read(use.access)) {
        buffer_needed[use.buffer.index] = true;
      }
    }
  }

  // The compile-time rules over the running passes in order (invariant 11,
  // declaration.hpp contract 3): a frame-only resource holds contents once a
  // running pass wrote it; an imported one holds them from the start.
  std::vector<bool> texture_written(texture_count);
  std::vector<bool> buffer_written(buffer_count);
  for (std::size_t i = 0; i < texture_count; ++i) {
    texture_written[i] = s.textures[i].imported;
  }
  for (std::size_t i = 0; i < buffer_count; ++i) {
    buffer_written[i] = s.buffers[i].imported;
  }
  for (std::size_t p = 0; p < pass_count; ++p) {
    if (!runs[p]) {
      continue;
    }
    const PassDeclaration& pass = s.passes[p];
    for (const TextureUse& use : pass.textures) {
      if (texture_written[use.texture.index]) {
        continue;
      }
      const std::string_view name = name_of(s.textures[use.texture.index]);
      if (is_read(use.access)) {
        return CompileResult{GraphError::UnwrittenFrameOnlyRead, pass.name, name};
      }
      if (states_contents(use.access) && use.contents == Contents::Keep) {
        return CompileResult{GraphError::FirstUseKeepsUndefinedContents, pass.name, name};
      }
    }
    for (const BufferUse& use : pass.buffers) {
      if (!buffer_written[use.buffer.index] && is_read(use.access)) {
        return CompileResult{GraphError::UnwrittenFrameOnlyRead, pass.name,
                             name_of(s.buffers[use.buffer.index])};
      }
    }
    for (const TextureUse& use : pass.textures) {
      if (is_write(use.access)) {
        texture_written[use.texture.index] = true;
      }
    }
    for (const BufferUse& use : pass.buffers) {
      if (is_write(use.access)) {
        buffer_written[use.buffer.index] = true;
      }
    }
  }
  // A ReadBack mark reads the resource after its last pass (declaration.hpp,
  // contract 7).
  for (std::size_t i = 0; i < texture_count; ++i) {
    if (s.texture_readbacks[i].staging != nullptr && !texture_written[i]) {
      return CompileResult{GraphError::UnwrittenFrameOnlyRead, {}, name_of(s.textures[i])};
    }
  }
  for (std::size_t i = 0; i < buffer_count; ++i) {
    if (s.buffer_readbacks[i].staging != nullptr && !buffer_written[i]) {
      return CompileResult{GraphError::UnwrittenFrameOnlyRead, {}, name_of(s.buffers[i])};
    }
  }

  for (std::size_t p = 0; p < pass_count; ++p) {
    if (!runs[p]) {
      continue;
    }
    RunningPass running{};
    running.pass = static_cast<PassIndex>(p);
    running.kind = s.passes[p].kind;
    plan.running.push_back(running);
    plan.names.push_back(s.passes[p].name);
  }
  const std::span<const RunningPass> running_passes = plan.running;

  // The use of a resource by a declared pass, if any (one at most:
  // declaration.hpp, contract 4).
  const auto texture_use = [&s](TextureHandle handle, PassIndex pass) -> const TextureUse* {
    for (const TextureUse& use : s.passes[pass].textures) {
      if (use.texture == handle) {
        return &use;
      }
    }
    return nullptr;
  };
  const auto buffer_use = [&s](BufferHandle handle, PassIndex pass) -> const BufferUse* {
    for (const BufferUse& use : s.passes[pass].buffers) {
      if (use.buffer == handle) {
        return &use;
      }
    }
    return nullptr;
  };

  // 2. Lifetimes (contract 2): first and last running pass, frame-only only.
  plan.texture_lifetimes.resize(texture_count);
  plan.buffer_lifetimes.resize(buffer_count);
  for (const RunningPass& running : running_passes) {
    for (const TextureUse& use : s.passes[running.pass].textures) {
      if (s.textures[use.texture.index].imported) {
        continue;
      }
      Lifetime& lifetime = plan.texture_lifetimes[use.texture.index];
      if (!lifetime.exists()) {
        lifetime.first = running.pass;
      }
      lifetime.last = running.pass;
    }
    for (const BufferUse& use : s.passes[running.pass].buffers) {
      if (s.buffers[use.buffer.index].imported) {
        continue;
      }
      Lifetime& lifetime = plan.buffer_lifetimes[use.buffer.index];
      if (!lifetime.exists()) {
        lifetime.first = running.pass;
      }
      lifetime.last = running.pass;
    }
  }

  // 3. Transitions and exit transitions (contract 3). A read-back imported
  // resource leaves its last pass for the copy, so its exit transition
  // starts from the state the copy left it in.
  std::vector<PlacedTransition> placed;
  for (std::size_t i = 0; i < texture_count; ++i) {
    const TextureHandle handle{static_cast<std::uint16_t>(i), s.frame};
    const TextureRecord& record = s.textures[i];
    const rhi::Access start = record.imported ? record.import.entry : rhi::Access::Unknown;
    rhi::Access last = place_transitions(
        running_passes, start,
        [&](PassIndex pass, Access& access) {
          const TextureUse* use = texture_use(handle, pass);
          if (use != nullptr) {
            access = use->access;
          }
          return use != nullptr;
        },
        [handle](rhi::Access before, rhi::Access after) {
          return ResourceTransition{handle, BufferHandle{}, before, after};
        },
        placed);
    if (!record.imported) {
      continue;
    }
    if (s.texture_readbacks[i].staging != nullptr) {
      last = after_readback(last);
    }
    const rhi::Access exit = record.presented ? rhi::Access::Present : record.import.exit;
    if (last != exit) {
      plan.exit_transitions.push_back(ResourceTransition{handle, BufferHandle{}, last, exit});
    }
  }
  for (std::size_t i = 0; i < buffer_count; ++i) {
    const BufferHandle handle{static_cast<std::uint16_t>(i), s.frame};
    const BufferRecord& record = s.buffers[i];
    const rhi::Access start = record.imported ? record.import.entry : rhi::Access::Unknown;
    rhi::Access last = place_transitions(
        running_passes, start,
        [&](PassIndex pass, Access& access) {
          const BufferUse* use = buffer_use(handle, pass);
          if (use != nullptr) {
            access = use->access;
          }
          return use != nullptr;
        },
        [handle](rhi::Access before, rhi::Access after) {
          return ResourceTransition{TextureHandle{}, handle, before, after};
        },
        placed);
    if (!record.imported) {
      continue;
    }
    if (s.buffer_readbacks[i].staging != nullptr) {
      last = after_readback(last);
    }
    if (last != record.import.exit) {
      plan.exit_transitions.push_back(
          ResourceTransition{TextureHandle{}, handle, last, record.import.exit});
    }
  }
  std::stable_sort(
      placed.begin(), placed.end(),
      [](const PlacedTransition& a, const PlacedTransition& b) { return a.running < b.running; });
  std::size_t next = 0;
  for (std::uint32_t r = 0; r < plan.running.size(); ++r) {
    RunningPass& running = plan.running[r];
    running.first_transition = static_cast<std::uint32_t>(plan.transitions.size());
    for (; next < placed.size() && placed[next].running == r; ++next) {
      plan.transitions.push_back(placed[next].transition);
    }
    running.transition_count =
        static_cast<std::uint32_t>(plan.transitions.size()) - running.first_transition;
  }

  // 4. Attachment actions (contract 4), in slot order with depth last.
  const auto used_after = [&](TextureHandle handle, std::uint32_t r) {
    for (std::uint32_t later = r + 1U; later < plan.running.size(); ++later) {
      if (texture_use(handle, plan.running[later].pass) != nullptr) {
        return true;
      }
    }
    return false;
  };
  for (std::uint32_t r = 0; r < plan.running.size(); ++r) {
    RunningPass& running = plan.running[r];
    running.first_attachment = static_cast<std::uint32_t>(plan.attachments.size());
    if (running.kind != PassKind::Raster) {
      continue;
    }
    for (const TextureUse& use : s.passes[running.pass].textures) {
      if (!is_attachment(use.access)) {
        continue;
      }
      const TextureRecord& record = s.textures[use.texture.index];
      AttachmentActions actions{};
      actions.texture = use.texture;
      actions.depth = use.access != Access::Attachment;
      actions.slot = actions.depth ? std::uint8_t{0} : use.slot;
      actions.depth_read_only = use.access == Access::DepthTest;
      switch (use.contents) {
      case Contents::Clear:
        actions.load = rhi::LoadAction::Clear;
        break;
      case Contents::Overwrite:
        actions.load = rhi::LoadAction::NoAction;
        break;
      case Contents::Keep:
        actions.load = rhi::LoadAction::Load;
        break;
      }
      const bool stored = used_after(use.texture, r) || record.imported ||
                          marked(record, s.texture_readbacks[use.texture.index]);
      actions.store = stored ? rhi::StoreAction::Store : rhi::StoreAction::NoAction;
      plan.attachments.push_back(actions);
      if (running.extent == rhi::Extent2{}) {
        running.extent = record.imported && record.import.texture != nullptr
                             ? rhi::Extent2{record.import.texture->description().extent.width,
                                            record.import.texture->description().extent.height}
                             : record.description.extent;
      }
    }
    const auto first = plan.attachments.begin() + running.first_attachment;
    std::stable_sort(first, plan.attachments.end(),
                     [](const AttachmentActions& a, const AttachmentActions& b) {
                       return a.depth != b.depth ? !a.depth : a.slot < b.slot;
                     });
    running.attachment_count =
        static_cast<std::uint32_t>(plan.attachments.size()) - running.first_attachment;
  }

  // 5. Schedule (contract 5).
  out.schedule = policy.schedule(plan, capabilities);
  return CompileResult{};
}

} // namespace bw::graph
