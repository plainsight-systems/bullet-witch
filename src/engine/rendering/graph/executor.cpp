#include "bw/rendering/graph/executor.hpp"

#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/compiled_frame.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/transitions.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Responsibility: execute (bw/rendering/graph/executor.hpp, contracts 1 to
// 4): per span and running pass, acquire, transition, begin, record, end and
// give back, then the readbacks and the exit transitions.
namespace bw::graph {

namespace {

// Usage bits from the accesses declared on a frame-only resource
// (declaration.hpp, contract 2).
[[nodiscard]] rhi::TextureUsage texture_usage_for(Access access) noexcept {
  switch (access) {
  case Access::Sampled:
    return rhi::TextureUsage::ShaderResource;
  case Access::Attachment:
    return rhi::TextureUsage::RenderTarget;
  case Access::DepthTest:
  case Access::DepthWrite:
    return rhi::TextureUsage::DepthStencil;
  case Access::ComputeWrite:
    return rhi::TextureUsage::UnorderedAccess;
  case Access::CopySource:
    return rhi::TextureUsage::CopySource;
  case Access::CopyDestination:
    return rhi::TextureUsage::CopyDestination;
  case Access::Arguments:
    return rhi::TextureUsage::None;
  }
  return rhi::TextureUsage::None;
}

// An Arguments use in a Raster pass also gives ShaderResource: its state
// carries the two shader-read stages (access.hpp, contract 2), so the buffer
// must allow them (declaration.hpp, contract 2).
[[nodiscard]] rhi::BufferUsage buffer_usage_for(Access access, PassKind kind) noexcept {
  switch (access) {
  case Access::Sampled:
    return rhi::BufferUsage::ShaderResource;
  case Access::ComputeWrite:
    return rhi::BufferUsage::UnorderedAccess;
  case Access::Arguments:
    return kind == PassKind::Raster
               ? rhi::BufferUsage::IndirectArguments | rhi::BufferUsage::ShaderResource
               : rhi::BufferUsage::IndirectArguments;
  case Access::CopySource:
    return rhi::BufferUsage::CopySource;
  case Access::CopyDestination:
    return rhi::BufferUsage::CopyDestination;
  case Access::Attachment:
  case Access::DepthTest:
  case Access::DepthWrite:
    return rhi::BufferUsage::None;
  }
  return rhi::BufferUsage::None;
}

// What the running passes declare on one frame-only resource: the usage over
// the frame and the state of its first use, which it is created in.
struct Derived {
  rhi::Access initial{rhi::Access::Unknown};
  bool found{};
};

class Executor {
public:
  Executor(const CompiledFrame& frame, FrameGraph& graph, rhi::Device& device,
           rhi::CommandList& list)
      : plan_(frame.plan), schedule_(frame.schedule), graph_(graph), device_(device), list_(list),
        texture_state_(graph.texture_count(), rhi::Access::Unknown),
        buffer_state_(graph.buffer_count(), rhi::Access::Unknown) {
    seed_imported_states();
  }

  ExecuteFailure run();

private:
  ExecuteFailure acquire(PassIndex pass);
  ExecuteFailure acquire_texture(TextureHandle handle);
  ExecuteFailure acquire_buffer(BufferHandle handle);
  void give_back(PassIndex pass);
  void add_transitions(const RunningPass& running);
  void add_transition(const ResourceTransition& transition);
  void flush_transitions();
  void begin_span(const RenderPassSpan& span);
  void record(const RunningPass& running);
  void read_back();
  void seed_imported_states();
  [[nodiscard]] TextureHandle texture_handle(std::size_t index) const noexcept {
    return graph_.texture_handle(static_cast<std::uint32_t>(index));
  }
  [[nodiscard]] BufferHandle buffer_handle(std::size_t index) const noexcept {
    return graph_.buffer_handle(static_cast<std::uint32_t>(index));
  }

  const FramePlan& plan_;
  const Schedule& schedule_;
  FrameGraph& graph_;
  rhi::Device& device_;
  rhi::CommandList& list_;
  // The state each resource is in at this point of the list: an imported
  // resource starts in its entry state, a frame-only one in the state it is
  // created in, and each transition recorded or skipped moves it to its
  // after state.
  std::vector<rhi::Access> texture_state_;
  std::vector<rhi::Access> buffer_state_;
  std::vector<rhi::Transition> batch_{};
};

// Contract 1: an imported resource enters the frame in the entry state its
// import names, so a read-back import no running pass touched leaves that
// state for CopySource.
void Executor::seed_imported_states() {
  for (std::size_t i = 0; i < texture_state_.size(); ++i) {
    const ImportedTexture* imported = graph_.imported_texture(texture_handle(i));
    if (imported != nullptr) {
      texture_state_[i] = imported->entry;
    }
  }
  for (std::size_t i = 0; i < buffer_state_.size(); ++i) {
    const ImportedBuffer* imported = graph_.imported_buffer(buffer_handle(i));
    if (imported != nullptr) {
      buffer_state_[i] = imported->entry;
    }
  }
}

ExecuteFailure Executor::acquire(PassIndex pass) {
  for (std::size_t i = 0; i < plan_.texture_lifetimes.size(); ++i) {
    const Lifetime& lifetime = plan_.texture_lifetimes[i];
    if (lifetime.exists() && lifetime.first == pass) {
      const ExecuteFailure failure = acquire_texture(texture_handle(i));
      if (!failure.ok()) {
        return failure;
      }
    }
  }
  for (std::size_t i = 0; i < plan_.buffer_lifetimes.size(); ++i) {
    const Lifetime& lifetime = plan_.buffer_lifetimes[i];
    if (lifetime.exists() && lifetime.first == pass) {
      const ExecuteFailure failure = acquire_buffer(buffer_handle(i));
      if (!failure.ok()) {
        return failure;
      }
    }
  }
  return ExecuteFailure{};
}

// Contract 1a and 3: the description the frame declared, the usage derived
// from the running passes' accesses, Memoryless when the schedule marks it
// and the device reports the capability, created in the state of its first
// use. A read-back texture is copied from, so it also has CopySource.
ExecuteFailure Executor::acquire_texture(TextureHandle handle) {
  const FrameTextureDescription* declared = graph_.texture_description(handle);
  rhi::TextureUsage usage = rhi::TextureUsage::None;
  Derived derived{};
  for (const RunningPass& running : plan_.running) {
    for (const TextureUse& use : graph_.texture_uses(running.pass)) {
      if (use.texture != handle) {
        continue;
      }
      usage = usage | texture_usage_for(use.access);
      if (!derived.found) {
        derived = Derived{rhi_access(use.access, running.kind), true};
      }
    }
  }
  if (graph_.texture_readbacks()[handle.index].staging != nullptr) {
    usage = usage | rhi::TextureUsage::CopySource;
  }
  if (device_.capabilities().texture_limits.memoryless) {
    for (const SpanAttachment& attachment : schedule_.attachments) {
      if (attachment.texture == handle && attachment.memoryless) {
        usage = usage | rhi::TextureUsage::Memoryless;
        break;
      }
    }
  }
  rhi::TextureDescription description{};
  description.extent = rhi::Extent3{declared->extent.width, declared->extent.height, 1};
  description.format = declared->format;
  description.usage = usage;
  description.initial_access = derived.initial;
  description.clear_value = declared->clear_value;
  description.debug_name = declared->debug_name;
  rhi::TextureResult created = device_.create_texture(description);
  if (!created.ok()) {
    return ExecuteFailure{ExecuteError::FrameOnlyTextureFailed, created.error(),
                          created.detail().empty() ? declared->debug_name : created.detail()};
  }
  graph_.acquire_texture(handle, std::move(created.value()));
  texture_state_[handle.index] = derived.initial;
  return ExecuteFailure{};
}

ExecuteFailure Executor::acquire_buffer(BufferHandle handle) {
  const FrameBufferDescription* declared = graph_.buffer_description(handle);
  rhi::BufferUsage usage = rhi::BufferUsage::None;
  Derived derived{};
  for (const RunningPass& running : plan_.running) {
    for (const BufferUse& use : graph_.buffer_uses(running.pass)) {
      if (use.buffer != handle) {
        continue;
      }
      usage = usage | buffer_usage_for(use.access, running.kind);
      if (!derived.found) {
        derived = Derived{rhi_access(use.access, running.kind), true};
      }
    }
  }
  if (graph_.buffer_readbacks()[handle.index].staging != nullptr) {
    usage = usage | rhi::BufferUsage::CopySource;
  }
  rhi::BufferDescription description{};
  description.size = declared->size;
  description.usage = usage;
  description.initial_access = derived.initial;
  description.debug_name = declared->debug_name;
  rhi::BufferResult created = device_.create_buffer(description);
  if (!created.ok()) {
    return ExecuteFailure{ExecuteError::FrameOnlyBufferFailed, created.error(),
                          created.detail().empty() ? declared->debug_name : created.detail()};
  }
  graph_.acquire_buffer(handle, std::move(created.value()));
  buffer_state_[handle.index] = derived.initial;
  return ExecuteFailure{};
}

// Contract 1f: released to the device's delete queue through the Ref. A
// read-back resource is given back after its copy instead.
void Executor::give_back(PassIndex pass) {
  for (std::size_t i = 0; i < plan_.texture_lifetimes.size(); ++i) {
    const Lifetime& lifetime = plan_.texture_lifetimes[i];
    if (lifetime.exists() && lifetime.last == pass &&
        graph_.texture_readbacks()[i].staging == nullptr) {
      graph_.release_texture(texture_handle(i));
    }
  }
  for (std::size_t i = 0; i < plan_.buffer_lifetimes.size(); ++i) {
    const Lifetime& lifetime = plan_.buffer_lifetimes[i];
    if (lifetime.exists() && lifetime.last == pass &&
        graph_.buffer_readbacks()[i].staging == nullptr) {
      graph_.release_buffer(buffer_handle(i));
    }
  }
}

void Executor::add_transitions(const RunningPass& running) {
  const std::span<const ResourceTransition> transitions =
      std::span<const ResourceTransition>(plan_.transitions)
          .subspan(running.first_transition, running.transition_count);
  for (const ResourceTransition& transition : transitions) {
    add_transition(transition);
  }
}

// Contract 1b: a transition whose before equals its after is not recorded.
void Executor::add_transition(const ResourceTransition& transition) {
  rhi::Viewable* resource = nullptr;
  if (transition.texture.valid()) {
    resource = graph_.texture(transition.texture);
    texture_state_[transition.texture.index] = transition.after;
  } else {
    resource = graph_.buffer(transition.buffer);
    buffer_state_[transition.buffer.index] = transition.after;
  }
  if (transition.before == transition.after) {
    return;
  }
  batch_.push_back(rhi::Transition{resource, transition.before, transition.after});
}

void Executor::flush_transitions() {
  if (batch_.empty()) {
    return;
  }
  list_.transition(rhi::TransitionBatch{std::span<const rhi::Transition>(batch_)});
  batch_.clear();
}

// Contract 1c: the span's attachments with their load and store actions,
// named for its first pass. The depth attachment's stencil plane, when its
// format has one, follows its depth actions: the game writes stencil in the
// pass that writes depth and transitions the texture whole.
void Executor::begin_span(const RenderPassSpan& span) {
  rhi::RenderPassDescription description{};
  const std::span<const SpanAttachment> attachments =
      std::span<const SpanAttachment>(schedule_.attachments)
          .subspan(span.first_attachment, span.attachment_count);
  for (const SpanAttachment& attachment : attachments) {
    rhi::Texture* texture = graph_.texture(attachment.texture);
    if (attachment.depth) {
      rhi::DepthStencilTarget& depth = description.depth_stencil;
      depth.texture = texture;
      depth.depth_load = attachment.load;
      depth.depth_store = attachment.store;
      depth.depth_read_only = attachment.depth_read_only;
      if (texture != nullptr && rhi::format_info(texture->description().format).has_stencil) {
        depth.stencil_load = attachment.load;
        depth.stencil_store = attachment.store;
        depth.stencil_read_only = attachment.depth_read_only;
      }
      continue;
    }
    description.colors[attachment.slot] =
        rhi::ColorTarget{texture, 0, 0, attachment.load, attachment.store};
    ++description.color_count;
  }
  const RunningPass& first = plan_.running[span.first_running];
  const PassDeclaration* pass = graph_.pass(first.pass);
  description.name = pass != nullptr ? pass->name : std::string_view{};
  list_.begin_render_pass(description);
  list_.set_viewport(rhi::ViewportRect{0.0F, 0.0F, static_cast<float>(first.extent.width),
                                       static_cast<float>(first.extent.height), 0.0F, 1.0F});
  list_.set_scissor(rhi::ScissorRect{0, 0, first.extent.width, first.extent.height});
}

void Executor::record(const RunningPass& running) {
  const PassDeclaration* pass = graph_.pass(running.pass);
  if (pass == nullptr) {
    return;
  }
  RecordContext context(graph_, list_, running.pass, running.extent);
  pass->record(context);
}

// After the last running pass: per ReadBack mark, a transition to
// CopySource unless the resource is in it, the copy and the fence; a
// frame-only resource read back is given back after its copy. The
// transition starts from the tracked state: the last running use's, or the
// entry state of an import no running pass touched (seed_imported_states).
void Executor::read_back() {
  const std::span<const ReadBack> textures = graph_.texture_readbacks();
  for (std::size_t i = 0; i < textures.size(); ++i) {
    const ReadBack& readback = textures[i];
    if (readback.staging == nullptr) {
      continue;
    }
    const TextureHandle handle = texture_handle(i);
    rhi::Texture* texture = graph_.texture(handle);
    const rhi::Access state = texture_state_[i];
    if (!rhi::has_all(state, rhi::Access::CopySource)) {
      const rhi::Transition to_copy{texture, state, rhi::Access::CopySource};
      list_.transition(rhi::TransitionBatch{std::span<const rhi::Transition>(&to_copy, 1)});
      texture_state_[i] = rhi::Access::CopySource;
    }
    list_.copy_texture_to_staging(*texture, 0, 0, *readback.staging, readback.staging_offset);
    if (readback.fence != nullptr) {
      list_.write_fence(*readback.fence);
    }
    if (!graph_.is_imported(handle)) {
      graph_.release_texture(handle);
    }
  }
  const std::span<const ReadBack> buffers = graph_.buffer_readbacks();
  for (std::size_t i = 0; i < buffers.size(); ++i) {
    const ReadBack& readback = buffers[i];
    if (readback.staging == nullptr) {
      continue;
    }
    const BufferHandle handle = buffer_handle(i);
    rhi::Buffer* buffer = graph_.buffer(handle);
    const rhi::Access state = buffer_state_[i];
    if (!rhi::has_all(state, rhi::Access::CopySource)) {
      const rhi::Transition to_copy{buffer, state, rhi::Access::CopySource};
      list_.transition(rhi::TransitionBatch{std::span<const rhi::Transition>(&to_copy, 1)});
      buffer_state_[i] = rhi::Access::CopySource;
    }
    list_.copy_buffer_to_staging(
        *buffer, *readback.staging,
        rhi::BufferCopy{0, readback.staging_offset, buffer->description().size});
    if (readback.fence != nullptr) {
      list_.write_fence(*readback.fence);
    }
    if (!graph_.is_imported(handle)) {
      graph_.release_buffer(handle);
    }
  }
}

// Contract 1: spans in order, and within them the running passes; a
// Compute or Copy pass between spans runs on its own.
ExecuteFailure Executor::run() {
  std::size_t next_span = 0;
  const RenderPassSpan* open = nullptr;
  for (std::uint32_t r = 0; r < plan_.running.size(); ++r) {
    const RunningPass& running = plan_.running[r];
    const ExecuteFailure acquired = acquire(running.pass);
    if (!acquired.ok()) {
      // The list is outside a render pass on return (contract 1).
      if (open != nullptr) {
        list_.end_render_pass();
      }
      return acquired;
    }
    if (running.kind != PassKind::Raster) {
      add_transitions(running);
      flush_transitions();
      record(running);
      give_back(running.pass);
      continue;
    }
    if (open == nullptr && next_span < schedule_.spans.size() &&
        schedule_.spans[next_span].first_running == r) {
      open = &schedule_.spans[next_span];
      ++next_span;
      for (std::uint32_t in_span = open->first_running; in_span <= open->last_running; ++in_span) {
        add_transitions(plan_.running[in_span]);
      }
      flush_transitions();
      begin_span(*open);
    }
    record(running);
    if (open != nullptr && open->last_running == r) {
      list_.end_render_pass();
      open = nullptr;
    }
    give_back(running.pass);
  }
  read_back();
  for (const ResourceTransition& transition : plan_.exit_transitions) {
    add_transition(transition);
  }
  flush_transitions();
  return ExecuteFailure{};
}

} // namespace

// The RHI creates and releases each frame-only resource on its own
// (executor.hpp, contract 2); the graph pools no storage.
ExecuteFailure execute(const CompiledFrame& frame, FrameGraph& graph, rhi::Device& device,
                       rhi::CommandList& list) {
  Executor executor(frame, graph, device, list);
  return executor.run();
}

} // namespace bw::graph
