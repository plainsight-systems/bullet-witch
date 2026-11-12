// Unit test: public contracts of bw/rendering/graph/ (access.hpp,
// builder.hpp, compiled_frame.hpp, schedule_policy.hpp), one scenario per
// CTest entry. Each scenario names the header declarations it asserts. No
// device: imported resources are local rhi::Texture objects, and a
// readback mark names a local staging buffer.
#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/compiled_frame.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/graph/schedule_policy.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace graph = bw::graph;
namespace rhi = bw::rhi;
using bw::testing::require;
using graph::Access;
using graph::Contents;
using graph::PassKind;
using graph::TextureHandle;
using graph::TextureUse;
using Scenario = bw::testing::Scenario<void>;

// An owner's texture as the graph sees it: the base class with its
// description, nothing behind it.
class LocalTexture final : public rhi::Texture {
public:
  LocalTexture(rhi::DeferredDeleteQueue& queue, const rhi::TextureDescription& description) noexcept
      : Texture(queue, description) {}
};

class LocalStaging final : public rhi::StagingBuffer {
public:
  explicit LocalStaging(rhi::DeferredDeleteQueue& queue) noexcept
      : StagingBuffer(queue, 1U << 16U, "readback") {}
};

constexpr rhi::Extent2 kExtent{64, 32};

rhi::TextureDescription owner_description(std::string_view name, rhi::TextureUsage usage,
                                          rhi::Access initial) {
  rhi::TextureDescription d{};
  d.extent = rhi::Extent3{kExtent.width, kExtent.height, 1};
  d.format = rhi::Format::R8G8B8A8Unorm;
  d.usage = usage;
  d.initial_access = initial;
  d.debug_name = name;
  return d;
}

graph::FrameTextureDescription color(std::string_view name, rhi::Extent2 extent = kExtent) {
  return graph::FrameTextureDescription{extent, rhi::Format::R8G8B8A8Unorm, {}, name};
}

graph::FrameTextureDescription depth(std::string_view name) {
  return graph::FrameTextureDescription{kExtent, rhi::Format::D32Float, {}, name};
}

// The owners' textures and the staging buffer a frame of a scenario imports
// or reads back into.
struct Owners {
  rhi::DeferredDeleteQueue queue{};
  LocalTexture target{queue, owner_description("target", rhi::TextureUsage::RenderTarget,
                                               rhi::Access::RenderTarget)};
  LocalTexture storage{queue, owner_description("storage", rhi::TextureUsage::UnorderedAccess,
                                                rhi::Access::UnorderedAccessCompute)};
  LocalTexture back_buffer{
      queue, owner_description("back buffer",
                               rhi::TextureUsage::RenderTarget | rhi::TextureUsage::BackBuffer,
                               rhi::Access::Present)};
  LocalStaging staging{queue};

  [[nodiscard]] graph::ReadBack readback() { return graph::ReadBack{&staging, nullptr, 0}; }
};

// A pass whose record function records nothing; the uses are copied by
// add_pass (declaration.hpp, contract 5).
void add(graph::FrameGraph& frame, std::string_view name, PassKind kind,
         std::initializer_list<TextureUse> textures,
         std::initializer_list<graph::BufferUse> buffers = {}) {
  static auto record_nothing = [](graph::RecordContext&) {};
  frame.add_pass(graph::PassDeclaration{
      name, kind, std::span<const TextureUse>(textures.begin(), textures.size()),
      std::span<const graph::BufferUse>(buffers.begin(), buffers.size()), record_nothing});
}

TextureUse attach(TextureHandle texture, Contents contents, std::uint8_t slot = 0) {
  return TextureUse{texture, Access::Attachment, contents, slot};
}
TextureUse use(TextureHandle texture, Access access, Contents contents = Contents::Keep) {
  return TextureUse{texture, access, contents, 0};
}

const graph::MetalSchedulePolicy kMetal{};

graph::CompileResult compile(graph::FrameGraph& frame, graph::CompiledFrame& out) {
  return frame.compile(kMetal, rhi::Capabilities{}, out);
}

void require_compiles(graph::FrameGraph& frame, graph::CompiledFrame& out) {
  const graph::CompileResult result = compile(frame, out);
  require(result.ok(), "compile fails with " + std::string(graph::graph_error_name(result.error)) +
                           " at " + std::string(result.pass) + " " + std::string(result.resource));
}

// The transitions a running pass carries for one texture.
std::vector<graph::ResourceTransition>
transitions_of(const graph::FramePlan& plan, std::uint32_t running, TextureHandle texture) {
  std::vector<graph::ResourceTransition> found;
  const graph::RunningPass& pass = plan.running.at(running);
  for (std::uint32_t i = 0; i < pass.transition_count; ++i) {
    const graph::ResourceTransition& t = plan.transitions.at(pass.first_transition + i);
    if (t.texture == texture) {
      found.push_back(t);
    }
  }
  return found;
}

graph::AttachmentActions actions_of(const graph::FramePlan& plan, std::uint32_t running,
                                    TextureHandle texture) {
  const graph::RunningPass& pass = plan.running.at(running);
  for (std::uint32_t i = 0; i < pass.attachment_count; ++i) {
    const graph::AttachmentActions& a = plan.attachments.at(pass.first_attachment + i);
    if (a.texture == texture) {
      return a;
    }
  }
  throw std::runtime_error("no attachment entry for the texture at running pass " +
                           std::to_string(running));
}

const graph::SpanAttachment& span_attachment(const graph::Schedule& schedule, std::size_t span,
                                             TextureHandle texture) {
  const graph::RenderPassSpan& s = schedule.spans.at(span);
  for (std::uint32_t i = 0; i < s.attachment_count; ++i) {
    const graph::SpanAttachment& a = schedule.attachments.at(s.first_attachment + i);
    if (a.texture == texture) {
      return a;
    }
  }
  throw std::runtime_error("no span attachment for the texture in span " + std::to_string(span));
}

// (builder.hpp, contract 2; compiled_frame.hpp, contract 1): a chain whose
// last output nothing reads and nothing marks runs no pass; marking it runs
// the whole chain, and ran() names the passes in declared order; a pass
// writing an imported texture runs though nothing marks it.
void culls_unread_passes() {
  Owners owners;
  graph::FrameGraph frame;
  graph::CompiledFrame out;
  const auto declare = [&frame] {
    const TextureHandle a = frame.create_texture(color("a"));
    const TextureHandle b = frame.create_texture(color("b"));
    const TextureHandle c = frame.create_texture(color("c"));
    add(frame, "first", PassKind::Raster, {attach(a, Contents::Clear)});
    add(frame, "second", PassKind::Raster, {use(a, Access::Sampled), attach(b, Contents::Clear)});
    add(frame, "third", PassKind::Raster, {use(b, Access::Sampled), attach(c, Contents::Clear)});
    return c;
  };
  declare();
  require_compiles(frame, out);
  require(out.plan.ran().empty(), "an unread, unmarked chain runs no pass");
  frame.reset();
  const TextureHandle last = declare();
  frame.mark_readback(last, owners.readback());
  require_compiles(frame, out);
  const std::vector<std::string_view> expected{"first", "second", "third"};
  require(out.plan.ran() == expected, "marking the last output runs the chain in order");
  frame.reset();
  const TextureHandle unmarked =
      frame.import_texture({&owners.target, rhi::Access::RenderTarget, rhi::Access::RenderTarget});
  add(frame, "imported", PassKind::Raster, {attach(unmarked, Contents::Clear)});
  require_compiles(frame, out);
  require(out.plan.ran() == std::vector<std::string_view>{"imported"},
          "a pass writing an unmarked imported texture runs");
}

// (compiled_frame.hpp, contract 2): a frame-only texture's lifetime is its
// first and last running pass; a culled pass extends none; an untouched
// resource has none.
void lifetimes_first_to_last() {
  Owners owners;
  graph::FrameGraph frame;
  const TextureHandle target =
      frame.import_texture({&owners.target, rhi::Access::RenderTarget, rhi::Access::RenderTarget});
  frame.mark_history(target);
  const TextureHandle t = frame.create_texture(color("t"));
  const TextureHandle culled_output = frame.create_texture(color("culled output"));
  const TextureHandle untouched = frame.create_texture(color("untouched"));
  add(frame, "0", PassKind::Raster, {attach(target, Contents::Clear)});
  add(frame, "1", PassKind::Raster, {attach(target, Contents::Keep)});
  add(frame, "2", PassKind::Raster, {attach(t, Contents::Clear)});
  add(frame, "3", PassKind::Raster, {attach(target, Contents::Keep)});
  add(frame, "4", PassKind::Raster, {use(t, Access::Sampled), attach(target, Contents::Keep)});
  add(frame, "culled", PassKind::Raster,
      {use(t, Access::Sampled), attach(culled_output, Contents::Clear)});
  graph::CompiledFrame out;
  require_compiles(frame, out);
  require(out.plan.texture_lifetimes.at(t.index) == graph::Lifetime{2, 4},
          "used by passes 2 and 4, and by the culled pass after them: lifetime {2, 4}");
  require(!out.plan.texture_lifetimes.at(untouched.index).exists(),
          "an untouched resource has no lifetime");
}

// (compiled_frame.hpp, contract 3; access.hpp, contracts 2 and 4): three
// consecutive reads share one state, the OR of their accesses, carried by
// one transition before the first; a write after them carries one more; the
// first use of a frame-only texture carries none.
void transitions_merge_reads() {
  Owners owners;
  graph::FrameGraph frame;
  const TextureHandle target =
      frame.import_texture({&owners.target, rhi::Access::RenderTarget, rhi::Access::RenderTarget});
  const TextureHandle storage = frame.import_texture(
      {&owners.storage, rhi::Access::UnorderedAccessCompute, rhi::Access::UnorderedAccessCompute});
  frame.mark_history(target);
  frame.mark_history(storage);
  const TextureHandle t = frame.create_texture(color("t"));
  add(frame, "write", PassKind::Raster, {attach(t, Contents::Clear)});
  add(frame, "read raster", PassKind::Raster,
      {use(t, Access::Sampled), attach(target, Contents::Clear)});
  add(frame, "read compute", PassKind::Compute,
      {use(t, Access::Sampled), use(storage, Access::ComputeWrite)});
  add(frame, "read raster again", PassKind::Raster,
      {use(t, Access::Sampled), attach(target, Contents::Keep)});
  add(frame, "write again", PassKind::Raster, {attach(t, Contents::Clear)});
  frame.mark_readback(t, owners.readback());
  graph::CompiledFrame out;
  require_compiles(frame, out);
  const rhi::Access shared = rhi::Access::ShaderResourcePixel |
                             rhi::Access::ShaderResourceNonPixel |
                             rhi::Access::ShaderResourceCompute;
  require(transitions_of(out.plan, 0, t).empty(), "the first use of a frame-only texture");
  const std::vector<graph::ResourceTransition> into_reads = transitions_of(out.plan, 1, t);
  require(into_reads.size() == 1U && into_reads[0].before == rhi::Access::RenderTarget &&
              into_reads[0].after == shared,
          "one transition before the first read, into the OR of the three reads");
  require(transitions_of(out.plan, 2, t).empty() && transitions_of(out.plan, 3, t).empty(),
          "the later reads carry none");
  const std::vector<graph::ResourceTransition> into_write = transitions_of(out.plan, 4, t);
  require(into_write.size() == 1U && into_write[0].before == shared &&
              into_write[0].after == rhi::Access::RenderTarget,
          "the write after the reads carries one more");
}

// (compiled_frame.hpp, contract 4): the load and store table, one line per
// row.
void actions_from_use() {
  Owners owners;
  graph::FrameGraph frame;
  const TextureHandle imported =
      frame.import_texture({&owners.target, rhi::Access::RenderTarget, rhi::Access::RenderTarget});
  frame.mark_history(imported);
  const TextureHandle a = frame.create_texture(color("a"));
  const TextureHandle b = frame.create_texture(color("b"));
  const TextureHandle d = frame.create_texture(depth("d"));
  const TextureHandle marked = frame.create_texture(color("marked"));
  add(frame, "0", PassKind::Raster,
      {attach(a, Contents::Clear), use(d, Access::DepthWrite, Contents::Clear)});
  add(frame, "1", PassKind::Raster, {attach(a, Contents::Keep), attach(b, Contents::Overwrite, 1)});
  add(frame, "2", PassKind::Raster, {use(d, Access::DepthTest), attach(imported, Contents::Keep)});
  add(frame, "3", PassKind::Raster, {use(a, Access::Sampled), attach(marked, Contents::Clear)});
  frame.mark_readback(marked, owners.readback());
  graph::CompiledFrame out;
  require_compiles(frame, out);
  const graph::FramePlan& plan = out.plan;
  require(actions_of(plan, 0, a).load == rhi::LoadAction::Clear, "Clear loads Clear");
  require(actions_of(plan, 1, b).load == rhi::LoadAction::NoAction, "Overwrite loads NoAction");
  require(actions_of(plan, 1, a).load == rhi::LoadAction::Load, "Keep with contents loads Load");
  require(actions_of(plan, 0, a).store == rhi::StoreAction::Store,
          "Store when a later running pass uses it");
  require(actions_of(plan, 3, marked).store == rhi::StoreAction::Store, "Store when marked");
  require(actions_of(plan, 2, imported).store == rhi::StoreAction::Store, "Store when imported");
  require(actions_of(plan, 1, b).store == rhi::StoreAction::NoAction,
          "NoAction when nothing later reads it");
  const graph::AttachmentActions tested = actions_of(plan, 2, d);
  require(tested.load == rhi::LoadAction::Load && tested.store == rhi::StoreAction::NoAction,
          "a depth texture used only to test loads, and stores by the same rule");
  require(tested.depth_read_only && !actions_of(plan, 0, d).depth_read_only,
          "depth_read_only on DepthTest only");
}

// (builder.hpp, GraphError): one row per enumerator, each declaration
// breaking that one rule, with the pass and resource the result names.
void errors_named() {
  struct Row {
    graph::GraphError error;
    void (*declare)(graph::FrameGraph&, Owners&);
    std::string_view pass;
    std::string_view resource;
  };
  const std::array<Row, 13> rows{{
      {graph::GraphError::HandleInvalid,
       [](graph::FrameGraph& f, Owners&) {
         // Made before a reset; a texture made after it has the same index,
         // so only the handle's frame tells them apart.
         const TextureHandle stale = f.create_texture(color("stale"));
         f.reset();
         static_cast<void>(f.create_texture(color("t")));
         add(f, "p", PassKind::Raster, {attach(stale, Contents::Clear)});
       },
       "p", ""},
      {graph::GraphError::AccessNotAllowedForPassKind,
       [](graph::FrameGraph& f, Owners&) {
         add(f, "p", PassKind::Copy, {use(f.create_texture(color("t")), Access::Sampled)});
       },
       "p", "t"},
      {graph::GraphError::ResourceUsedTwiceInOnePass,
       [](graph::FrameGraph& f, Owners&) {
         const TextureHandle t = f.create_texture(color("t"));
         add(f, "p", PassKind::Raster, {attach(t, Contents::Clear), use(t, Access::Sampled)});
       },
       "p", "t"},
      {graph::GraphError::RasterPassWithoutAttachment,
       [](graph::FrameGraph& f, Owners&) {
         add(f, "p", PassKind::Raster, {use(f.create_texture(color("t")), Access::Sampled)});
       },
       "p", ""},
      {graph::GraphError::AttachmentSlotInvalid,
       [](graph::FrameGraph& f, Owners&) {
         add(f, "p", PassKind::Raster,
             {attach(f.create_texture(color("t")), Contents::Clear, rhi::kMaxRenderTargets)});
       },
       "p", "t"},
      {graph::GraphError::AttachmentExtentMismatch,
       [](graph::FrameGraph& f, Owners&) {
         const TextureHandle t = f.create_texture(color("t"));
         const TextureHandle s = f.create_texture(color("s", rhi::Extent2{32, 16}));
         add(f, "p", PassKind::Raster, {attach(t, Contents::Clear), attach(s, Contents::Clear, 1)});
       },
       "p", "s"},
      {graph::GraphError::AttachmentFormatMismatch,
       [](graph::FrameGraph& f, Owners&) {
         add(f, "p", PassKind::Raster,
             {use(f.create_texture(color("t")), Access::DepthWrite, Contents::Clear)});
       },
       "p", "t"},
      {graph::GraphError::ContentsRequireAttachmentWrite,
       [](graph::FrameGraph& f, Owners&) {
         const TextureHandle t = f.create_texture(color("t"));
         const TextureHandle s = f.create_texture(color("s"));
         add(f, "p", PassKind::Raster,
             {attach(t, Contents::Clear), use(s, Access::Sampled, Contents::Clear)});
       },
       "p", "s"},
      {graph::GraphError::FirstUseKeepsUndefinedContents,
       [](graph::FrameGraph& f, Owners& o) {
         const TextureHandle t = f.create_texture(color("t"));
         add(f, "p", PassKind::Raster, {attach(t, Contents::Keep)});
         f.mark_readback(t, o.readback());
       },
       "p", "t"},
      {graph::GraphError::UnwrittenFrameOnlyRead,
       [](graph::FrameGraph& f, Owners& o) {
         const TextureHandle target =
             f.import_texture({&o.target, rhi::Access::RenderTarget, rhi::Access::RenderTarget});
         f.mark_history(target);
         add(f, "p", PassKind::Raster,
             {use(f.create_texture(color("t")), Access::Sampled), attach(target, Contents::Clear)});
       },
       "p", "t"},
      {graph::GraphError::ImportedStateInvalid,
       [](graph::FrameGraph& f, Owners& o) {
         static_cast<void>(
             f.import_texture({&o.target, rhi::Access::Unknown, rhi::Access::RenderTarget}));
       },
       "", "target"},
      {graph::GraphError::FrameOnlyCannotLeave,
       [](graph::FrameGraph& f, Owners&) { f.mark_history(f.create_texture(color("t"))); }, "",
       "t"},
      {graph::GraphError::PresentedNotBackBuffer,
       [](graph::FrameGraph& f, Owners& o) {
         f.mark_presented(
             f.import_texture({&o.target, rhi::Access::RenderTarget, rhi::Access::RenderTarget}));
       },
       "", "target"},
  }};
  Owners owners;
  graph::FrameGraph frame;
  graph::CompiledFrame out;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    frame.reset();
    rows[i].declare(frame, owners);
    const graph::CompileResult result = compile(frame, out);
    require(result.error == rows[i].error, i,
            "named " + std::string(graph::graph_error_name(result.error)) + ", expected " +
                std::string(graph::graph_error_name(rows[i].error)));
    // An imported texture is named by its RHI debug name, which is empty in
    // a build without the debug surface (rhi/resources.hpp, DebugName).
    const std::string_view resource =
        rows[i].resource == "target" ? owners.target.debug_name() : rows[i].resource;
    require(result.pass == rows[i].pass && result.resource == resource, i,
            "names pass '" + std::string(result.pass) + "' and resource '" +
                std::string(result.resource) + "'");
  }
}

// (compiled_frame.hpp, contract 3, exit transitions; declaration.hpp,
// contracts 6 and 7): an imported texture's first transition leaves its
// entry state and its exit transition reaches its exit state; a Presented
// mark makes the exit state Present; a read-back import no pass touches is
// copied from its entry state moved to CopySource, so its exit transition
// starts there (executor.hpp, contract 1).
void imported_enters_and_leaves() {
  Owners owners;
  graph::FrameGraph frame;
  const TextureHandle target = frame.import_texture(
      {&owners.target, rhi::Access::ShaderResourcePixel, rhi::Access::CopySource});
  const TextureHandle back_buffer =
      frame.import_texture({&owners.back_buffer, rhi::Access::Present, rhi::Access::RenderTarget});
  const TextureHandle untouched = frame.import_texture(
      {&owners.storage, rhi::Access::UnorderedAccessCompute, rhi::Access::UnorderedAccessCompute});
  frame.mark_history(target);
  frame.mark_presented(back_buffer);
  frame.mark_readback(untouched, owners.readback());
  add(frame, "target", PassKind::Raster, {attach(target, Contents::Keep)});
  add(frame, "back buffer", PassKind::Raster, {attach(back_buffer, Contents::Clear)});
  graph::CompiledFrame out;
  require_compiles(frame, out);
  const std::vector<graph::ResourceTransition> enters = transitions_of(out.plan, 0, target);
  require(enters.size() == 1U && enters[0].before == rhi::Access::ShaderResourcePixel &&
              enters[0].after == rhi::Access::RenderTarget,
          "the first transition leaves the entry state");
  bool target_leaves = false;
  bool back_buffer_leaves = false;
  bool untouched_leaves = false;
  for (const graph::ResourceTransition& t : out.plan.exit_transitions) {
    target_leaves = target_leaves || (t.texture == target && t.after == rhi::Access::CopySource &&
                                      t.before == rhi::Access::RenderTarget);
    back_buffer_leaves =
        back_buffer_leaves || (t.texture == back_buffer && t.after == rhi::Access::Present &&
                               t.before == rhi::Access::RenderTarget);
    untouched_leaves =
        untouched_leaves || (t.texture == untouched && t.before == rhi::Access::CopySource &&
                             t.after == rhi::Access::UnorderedAccessCompute);
  }
  require(target_leaves, "the exit transition reaches the exit state");
  require(back_buffer_leaves, "a presented texture leaves in Present whatever the import named");
  require(untouched_leaves,
          "a read-back import no pass touches leaves its entry state for the copy, then exits");
}

// (schedule_policy.hpp, contracts 3 and 4): adjacent Raster passes on the
// same attachments that keep or overwrite them become one span; a Compute
// pass between, a Clear in the later pass or a ComputeWrite use splits them; memoryless
// only on an attachment whose lifetime is inside one span and is not
// stored.
void metal_policy_merges_shared_attachments() {
  Owners owners;
  graph::FrameGraph frame;
  graph::CompiledFrame out;
  enum class Between : std::uint8_t { Nothing, Compute, Clear, Overwrite, ComputeWrite };
  struct Declared {
    TextureHandle target;
    TextureHandle d;
  };
  const auto declare = [&](Between between) {
    frame.reset();
    const TextureHandle target = frame.import_texture(
        {&owners.target, rhi::Access::RenderTarget, rhi::Access::RenderTarget});
    const TextureHandle storage =
        frame.import_texture({&owners.storage, rhi::Access::UnorderedAccessCompute,
                              rhi::Access::UnorderedAccessCompute});
    frame.mark_history(target);
    frame.mark_history(storage);
    const TextureHandle d = frame.create_texture(depth("d"));
    add(frame, "first", PassKind::Raster,
        {attach(target, Contents::Clear), use(d, Access::DepthWrite, Contents::Clear)});
    if (between == Between::Compute) {
      add(frame, "compute", PassKind::Compute, {use(storage, Access::ComputeWrite)});
    }
    if (between == Between::ComputeWrite) {
      const TextureHandle written = frame.create_texture(color("written"));
      frame.mark_readback(written, owners.readback());
      add(frame, "second", PassKind::Raster,
          {attach(target, Contents::Keep), use(d, Access::DepthWrite, Contents::Keep),
           use(written, Access::ComputeWrite)});
    } else {
      Contents later = Contents::Keep;
      if (between == Between::Clear) {
        later = Contents::Clear;
      } else if (between == Between::Overwrite) {
        later = Contents::Overwrite;
      }
      add(frame, "second", PassKind::Raster,
          {attach(target, later), use(d, Access::DepthWrite, Contents::Keep)});
    }
    require_compiles(frame, out);
    return Declared{target, d};
  };

  const Declared merged = declare(Between::Nothing);
  require(out.schedule.spans.size() == 1U && out.schedule.spans[0].first_running == 0U &&
              out.schedule.spans[0].last_running == 1U,
          "two mergeable passes are one span");
  require(span_attachment(out.schedule, 0, merged.d).memoryless,
          "the depth texture lives inside the span and is not stored: memoryless");
  require(!span_attachment(out.schedule, 0, merged.target).memoryless,
          "the stored, imported target is not memoryless");

  const Declared split = declare(Between::Compute);
  require(out.schedule.spans.size() == 2U, "a Compute pass between splits");
  require(!span_attachment(out.schedule, 0, split.d).memoryless &&
              !span_attachment(out.schedule, 1, split.d).memoryless,
          "a texture whose lifetime spans two spans is not memoryless");

  declare(Between::Overwrite);
  require(out.schedule.spans.size() == 1U, "an Overwrite in the later pass merges");
  declare(Between::Clear);
  require(out.schedule.spans.size() == 2U, "a Clear in the later pass splits");

  declare(Between::ComputeWrite);
  require(out.schedule.spans.size() == 2U, "a ComputeWrite use splits");
}

// (access.hpp, contracts 1 and 2): is_allowed and rhi_access, one row per
// (Access, PassKind) cell; rhi_access is read for the allowed cells.
void access_rhi_access_table() {
  using A = rhi::Access;
  struct Row {
    Access access;
    PassKind kind;
    bool allowed;
    A state;
  };
  const A raster_sampled = A::ShaderResourcePixel | A::ShaderResourceNonPixel;
  const std::array<Row, 24> rows{{
      {Access::Sampled, PassKind::Raster, true, raster_sampled},
      {Access::Sampled, PassKind::Compute, true, A::ShaderResourceCompute},
      {Access::Sampled, PassKind::Copy, false, A::Unknown},
      {Access::Attachment, PassKind::Raster, true, A::RenderTarget},
      {Access::Attachment, PassKind::Compute, false, A::Unknown},
      {Access::Attachment, PassKind::Copy, false, A::Unknown},
      {Access::DepthTest, PassKind::Raster, true, A::DepthStencilRead},
      {Access::DepthTest, PassKind::Compute, false, A::Unknown},
      {Access::DepthTest, PassKind::Copy, false, A::Unknown},
      {Access::DepthWrite, PassKind::Raster, true, A::DepthStencilWrite},
      {Access::DepthWrite, PassKind::Compute, false, A::Unknown},
      {Access::DepthWrite, PassKind::Copy, false, A::Unknown},
      {Access::ComputeWrite, PassKind::Raster, true, A::UnorderedAccessGraphics},
      {Access::ComputeWrite, PassKind::Compute, true, A::UnorderedAccessCompute},
      {Access::ComputeWrite, PassKind::Copy, false, A::Unknown},
      {Access::Arguments, PassKind::Raster, true, A::IndirectArguments | raster_sampled},
      {Access::Arguments, PassKind::Compute, true, A::IndirectArguments},
      {Access::Arguments, PassKind::Copy, false, A::Unknown},
      {Access::CopySource, PassKind::Raster, false, A::Unknown},
      {Access::CopySource, PassKind::Compute, false, A::Unknown},
      {Access::CopySource, PassKind::Copy, true, A::CopySource},
      {Access::CopyDestination, PassKind::Raster, false, A::Unknown},
      {Access::CopyDestination, PassKind::Compute, false, A::Unknown},
      {Access::CopyDestination, PassKind::Copy, true, A::CopyDestination},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    require(graph::is_allowed(row.access, row.kind) == row.allowed, i, "is_allowed");
    require(!row.allowed || graph::rhi_access(row.access, row.kind) == row.state, i, "rhi_access");
  }
}

constexpr std::array scenarios{
    Scenario{"culls_unread_passes", culls_unread_passes},
    Scenario{"lifetimes_first_to_last", lifetimes_first_to_last},
    Scenario{"transitions_merge_reads", transitions_merge_reads},
    Scenario{"actions_from_use", actions_from_use},
    Scenario{"errors_named", errors_named},
    Scenario{"imported_enters_and_leaves", imported_enters_and_leaves},
    Scenario{"metal_policy_merges_shared_attachments", metal_policy_merges_shared_attachments},
    Scenario{"access.rhi_access_table", access_rhi_access_table},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
