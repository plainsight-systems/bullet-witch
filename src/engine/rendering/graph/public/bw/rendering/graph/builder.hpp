#ifndef BW_RENDERING_GRAPH_BUILDER_HPP
#define BW_RENDERING_GRAPH_BUILDER_HPP

#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/compiled_frame.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/graph/schedule_policy.hpp"
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

// Responsibility: the per-frame graph (docs/architecture/render-graph.md,
// invariants 2, 4, 5, 7, 8, 9 and 11): the scene renderer declares frame-only
// resources, imports, passes and marks in frame order, compile derives the
// plan of compiled_frame.hpp and asks the platform's policy for the
// schedule, and execute (executor.hpp) records it. The graph is declared
// again every frame (invariant 2); one FrameGraph object is reused across
// frames and keeps its storage.
//
// Contract.
//   1. Declaration order is frame order. Passes run in the order they were
//      added (invariant 2); nothing reorders them. create_*, import_* and
//      add_pass return handles or nothing: a declaration that breaks a rule
//      of declaration.hpp is recorded and reported by compile, so a scene
//      declares its frame without checking each call. The first rule
//      broken, in declaration order, is the one reported.
//   2. compile derives, in this order: the running passes (invariant 4),
//      the lifetimes (invariant 5), the transitions and exit transitions
//      (invariants 7 and 8), the attachment actions (invariant 9), then the
//      schedule from the policy (invariant 10). On success `out` holds the
//      compiled frame and the result is ok(); on a declaration or compile
//      error the result names the error, the pass and the resource
//      concerned (empty when none applies) and `out` is unspecified.
//      GraphError is the closed list of named errors; each enumerator is
//      one rule, stated at the enumerator.
//   3. reset() forgets the frame: the frame number advances (a 16-bit
//      count that wraps), so every handle of the frame is invalid
//      afterwards, every imported pointer is dropped, and the storage is
//      kept for the next frame. A FrameGraph is declared, compiled, executed and reset
//      by one thread.
//   4. The execution side, called by execute and RecordContext only:
//      texture(handle) and buffer(handle) are the RHI objects of this frame
//      for a handle, the imported object or the frame-only object acquired
//      for the pass being executed, null before acquire and after give-back;
//      acquire_* and release_* hand a frame-only resource in and out. A
//      scene does not call these.
//
// compile and execute are two calls, so the host runs the policy and the
// validation tests read the plan without a device. One thread declares and
// records the graph, for one queue and one GPU; passes hand each other handles
// directly; a buffer is created by the RHI with its initial data
// (rhi/resources.hpp); no frame-only resource changes owner; and the RHI's
// frame boundary does the per-frame pool upkeep (rhi.md invariant 8).
// A pass uses a resource at most once (invariant 1), so a second use is the
// error ResourceUsedTwiceInOnePass rather than a merge of the two. The graph
// checks what it declares; the seam for anything a shader reads that the
// declaration does not name is declaration.hpp contract 5.
// The setup phase is cheap, so the graph is declared again every frame.
//
// Core guidelines read: I.1 and E.27 (the result names the error, the pass
// and the resource; no exception, no log-and-continue), C.67 (the graph is
// not copyable: it holds a frame's observers), R.3 and C.32 (imported
// pointers and the policy are observers; the owner keeps them alive through
// the frame), F.7, Enum.3. Performance guidelines read: MEM.1 (one object
// reused, vectors reset not freed), CDSA.26 (passes and uses in flat
// arrays indexed by handle), CACHE.3.
namespace bw::graph {

enum class GraphError : std::uint8_t {
  None,
  // A use, mark or import names a handle not created or imported in this
  // frame (declaration.hpp, contract 1); the result names the pass and no
  // resource.
  HandleInvalid,
  // access.hpp, contract 1.
  AccessNotAllowedForPassKind,
  // One pass names one resource twice (declaration.hpp, contract 4).
  ResourceUsedTwiceInOnePass,
  // A Raster pass with no Attachment, DepthTest or DepthWrite use.
  RasterPassWithoutAttachment,
  // A color slot at or past rhi::kMaxRenderTargets, two attachments on one
  // slot, or slots not packed from 0.
  AttachmentSlotInvalid,
  // The attachments of one pass differ in extent; the later attachment in
  // declaration order is the resource named.
  AttachmentExtentMismatch,
  // DepthTest or DepthWrite on a texture whose format is not a depth
  // format, or Attachment on one that is.
  AttachmentFormatMismatch,
  // Clear or Overwrite on a use that is not Attachment or DepthWrite.
  ContentsRequireAttachmentWrite,
  // A frame-only texture's first running use is an Attachment or DepthWrite
  // with Keep (declaration.hpp, contract 3).
  FirstUseKeepsUndefinedContents,
  // A read, or a ReadBack mark, of a frame-only resource no running pass
  // wrote before it (invariant 11).
  UnwrittenFrameOnlyRead,
  // An import whose pointer is null or whose entry or exit is Unknown or
  // not a valid access.
  ImportedStateInvalid,
  // Presented or History on a frame-only resource (declaration.hpp,
  // contract 7).
  FrameOnlyCannotLeave,
  // Presented on a texture that is not a viewport's back buffer
  // (rhi/resources.hpp, TextureUsage::BackBuffer).
  PresentedNotBackBuffer,
};

[[nodiscard]] constexpr std::string_view graph_error_name(GraphError error) noexcept {
  switch (error) {
  case GraphError::None:
    return "None";
  case GraphError::HandleInvalid:
    return "HandleInvalid";
  case GraphError::AccessNotAllowedForPassKind:
    return "AccessNotAllowedForPassKind";
  case GraphError::ResourceUsedTwiceInOnePass:
    return "ResourceUsedTwiceInOnePass";
  case GraphError::RasterPassWithoutAttachment:
    return "RasterPassWithoutAttachment";
  case GraphError::AttachmentSlotInvalid:
    return "AttachmentSlotInvalid";
  case GraphError::AttachmentExtentMismatch:
    return "AttachmentExtentMismatch";
  case GraphError::AttachmentFormatMismatch:
    return "AttachmentFormatMismatch";
  case GraphError::ContentsRequireAttachmentWrite:
    return "ContentsRequireAttachmentWrite";
  case GraphError::FirstUseKeepsUndefinedContents:
    return "FirstUseKeepsUndefinedContents";
  case GraphError::UnwrittenFrameOnlyRead:
    return "UnwrittenFrameOnlyRead";
  case GraphError::ImportedStateInvalid:
    return "ImportedStateInvalid";
  case GraphError::FrameOnlyCannotLeave:
    return "FrameOnlyCannotLeave";
  case GraphError::PresentedNotBackBuffer:
    return "PresentedNotBackBuffer";
  }
  return {};
}

struct CompileResult {
  GraphError error{GraphError::None};
  // The pass whose declaration broke the rule; empty for a mark or import.
  std::string_view pass{};
  // The resource's debug name; empty when no resource applies.
  std::string_view resource{};
  [[nodiscard]] constexpr bool ok() const noexcept { return error == GraphError::None; }
};

class FrameGraph {
public:
  FrameGraph();
  FrameGraph(const FrameGraph&) = delete;
  FrameGraph& operator=(const FrameGraph&) = delete;
  ~FrameGraph();

  // Declaration (declaration.hpp). The description's debug name, the pass
  // declaration's name and its record callable are borrowed until reset;
  // the uses are copied.
  [[nodiscard]] TextureHandle create_texture(const FrameTextureDescription& description);
  [[nodiscard]] BufferHandle create_buffer(const FrameBufferDescription& description);
  [[nodiscard]] TextureHandle import_texture(const ImportedTexture& imported);
  [[nodiscard]] BufferHandle import_buffer(const ImportedBuffer& imported);
  void add_pass(const PassDeclaration& pass);
  void mark_presented(TextureHandle texture);
  void mark_history(TextureHandle texture);
  void mark_history(BufferHandle buffer);
  void mark_readback(TextureHandle texture, const ReadBack& readback);
  void mark_readback(BufferHandle buffer, const ReadBack& readback);

  // Compile (contract 2).
  [[nodiscard]] CompileResult compile(const SchedulePolicy& policy,
                                      const rhi::Capabilities& capabilities, CompiledFrame& out);

  void reset() noexcept;

  // What was declared, for the executor and the tests: the counts of
  // frame-only and imported resources and of passes in this frame.
  [[nodiscard]] std::uint32_t texture_count() const noexcept;
  [[nodiscard]] std::uint32_t buffer_count() const noexcept;
  [[nodiscard]] std::uint32_t pass_count() const noexcept;
  [[nodiscard]] bool is_imported(TextureHandle texture) const noexcept;
  [[nodiscard]] bool is_imported(BufferHandle buffer) const noexcept;
  // The handle of this frame for a resource index below the count; an
  // invalid handle for one at or past it.
  [[nodiscard]] TextureHandle texture_handle(std::uint32_t index) const noexcept;
  [[nodiscard]] BufferHandle buffer_handle(std::uint32_t index) const noexcept;
  // The import as declared (object, entry, exit); null for a frame-only
  // resource or an invalid handle.
  [[nodiscard]] const ImportedTexture* imported_texture(TextureHandle texture) const noexcept;
  [[nodiscard]] const ImportedBuffer* imported_buffer(BufferHandle buffer) const noexcept;
  [[nodiscard]] const FrameTextureDescription*
  texture_description(TextureHandle texture) const noexcept;
  [[nodiscard]] const FrameBufferDescription*
  buffer_description(BufferHandle buffer) const noexcept;
  // Every use of a pass, in declaration order, viewing the builder's copy.
  [[nodiscard]] std::span<const TextureUse> texture_uses(PassIndex pass) const noexcept;
  [[nodiscard]] std::span<const BufferUse> buffer_uses(PassIndex pass) const noexcept;
  // The pass as added, its use spans viewing the builder's copy; null for
  // an index at or past pass_count().
  [[nodiscard]] const PassDeclaration* pass(PassIndex pass) const noexcept;
  [[nodiscard]] std::span<const ReadBack> texture_readbacks() const noexcept;
  [[nodiscard]] std::span<const ReadBack> buffer_readbacks() const noexcept;

  // Execution side (contract 4).
  [[nodiscard]] rhi::Texture* texture(TextureHandle texture) const noexcept;
  [[nodiscard]] rhi::Buffer* buffer(BufferHandle buffer) const noexcept;
  void acquire_texture(TextureHandle texture, rhi::Ref<rhi::Texture> acquired) noexcept;
  void acquire_buffer(BufferHandle buffer, rhi::Ref<rhi::Buffer> acquired) noexcept;
  void release_texture(TextureHandle texture) noexcept;
  void release_buffer(BufferHandle buffer) noexcept;

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace bw::graph

#endif
