#ifndef BW_RENDERING_GRAPH_DECLARATION_HPP
#define BW_RENDERING_GRAPH_DECLARATION_HPP

#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"

#include <concepts>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

// Responsibility: what a pass declares (docs/architecture/render-graph.md,
// invariants 1, 3, 7 and 9): its name and kind, every resource it touches
// with one Access, the frame-only textures and buffers a frame creates, the
// resources a frame imports with the state they enter and leave in, and the
// marks for outputs that leave the frame. Declarations are values; the
// per-frame builder (builder.hpp) records them.
//
// Contract.
//   1. Handles. A TextureHandle or BufferHandle names one resource of one
//      frame: a frame-only resource created by the builder or an imported
//      one. A handle carries the number of the frame that made it
//      (builder.hpp, contract 3); one whose frame is not the builder's
//      current frame, whose index is not one of the frame's, or that is
//      value-initialized is invalid, and a declaration or mark naming one
//      is GraphError::HandleInvalid.
//   2. Frame-only descriptions (invariant 5). FrameTextureDescription names
//      extent, format, the clear value and the debug name; the texture is
//      one mip, one slice, one sample. FrameBufferDescription names size
//      and debug name; a frame-only buffer is byte addressed. The RHI usage of a frame-only
//      resource is derived from the accesses declared on it over the frame, never written by a
//      pass: Sampled gives ShaderResource, Attachment gives RenderTarget, DepthTest and DepthWrite
//      give DepthStencil, ComputeWrite gives UnorderedAccess, Arguments gives IndirectArguments
//      and, in a Raster pass, ShaderResource with it, the copies give CopySource and
//      CopyDestination. Contents at first use are undefined (invariant 5): a read before any
//      running pass wrote the resource is GraphError::UnwrittenFrameOnlyRead (invariant 11).
//   3. Contents (invariant 9). An Attachment or DepthWrite use states what
//      the pass does to the texture's contents before it draws: Clear
//      (to the texture's clear value), Overwrite (every pixel is written,
//      what was there is not read) or Keep (the pass adds to what is
//      there). No pass writes a load or store action; the graph derives
//      them (compiled_frame.hpp, "Load and store actions"). A frame-only
//      texture whose first running use is an Attachment or DepthWrite with
//      Keep reads undefined contents and is
//      GraphError::FirstUseKeepsUndefinedContents. Contents other than Keep
//      on any other access is GraphError::ContentsRequireAttachmentWrite.
//   4. Uses. A TextureUse is a handle, an Access, the Contents and, for
//      Attachment, the color slot (0 .. rhi::kMaxRenderTargets - 1, slots
//      packed from 0 within a pass; GraphError::AttachmentSlotInvalid
//      otherwise). DepthTest and DepthWrite bind the pass's one depth
//      attachment and have no slot. A BufferUse is a handle and an Access.
//      One pass names one resource at most once (invariant 1: no pass reads
//      a texture it writes in the same pass); a second use of a handle in
//      one pass is GraphError::ResourceUsedTwiceInOnePass. An access the
//      kind does not admit (access.hpp, contract 1) is
//      GraphError::AccessNotAllowedForPassKind. A Raster pass without an
//      attachment use is GraphError::RasterPassWithoutAttachment. The
//      attachments of one pass have one extent
//      (GraphError::AttachmentExtentMismatch); a depth access on a texture
//      whose format is not a depth format, or an Attachment on one that is,
//      is GraphError::AttachmentFormatMismatch.
//   5. A PassDeclaration is a name, a kind, the texture and buffer uses and
//      a record function. The builder copies the uses, so the use arrays
//      may die after add_pass; the name and the callable behind the record
//      function are borrowed and live until the frame is executed.
//      The record function is called once per running pass, inside the
//      render pass the graph opened for a Raster pass with the viewport and
//      scissor set to the attachment extent; it records draws, dispatches
//      or copies through RecordContext::list() and resolves its declared
//      handles through RecordContext::texture and buffer. A handle the pass
//      did not declare resolves to null: a pass touches only what it
//      declared (invariant 1), and the graph cannot see what a shader reads
//      through a descriptor index, so this is the one seam it checks.
//   6. Imported resources (invariant 7). An ImportedTexture or
//      ImportedBuffer names the owner's resource with the access it is in
//      when the frame begins (entry) and the access the owner asks for when
//      the frame ends (exit). Entry and exit are states the RHI accepts
//      (rhi/access.hpp, is_valid_access, never Unknown) and the pointer is
//      not null, else GraphError::ImportedStateInvalid. An imported resource is never
//      reused for anything else: its handle is its own for the frame.
//   7. Leaving the frame (invariant 3), three marks on the builder
//      (builder.hpp, mark_presented, mark_history, mark_readback).
//      Presented: the texture is the
//      viewport's back buffer and leaves in rhi::Access::Present, which is
//      its exit state whatever the import named. History: the owner reads
//      the resource next frame; it leaves in its exit state. ReadBack: the
//      resource is copied to the staging buffer at the offset given and the
//      fence is written after the copy, through the RHI's readback path
//      (rhi.md, "Readback"); the caller locks the staging buffer through
//      the fence. Presented and History are marks on imported resources,
//      whose owner holds them past the frame; on a frame-only resource
//      either is GraphError::FrameOnlyCannotLeave. ReadBack is a read of
//      the resource after its last pass, so a frame-only resource read back
//      must have been written (contract 2).
//
// A frame-only texture lives one frame, is always tracked and carries no
// compression flag; a frame-only buffer's usage is derived from its declared
// accesses. Every frame-only texture of the pass list is one 2D mip
// (scene-renderer.md, "The frame"), and the bloom levels reduce inside one
// pass.
// An attachment declares Contents in place of a load action, and the action is
// derived (contract 3). There is no multisample resolve target and no separate
// stencil load action: stencil is written with depth in the one pass that uses
// it.
// The entry state is an argument of the import, not read from the resource,
// because invariant 7 has the owner state it.
// A history texture is owned by the system that reads it and imported each
// frame (render-graph.md, "Imported and exported resources"), so no frame-only
// resource changes owner.
//
// Core guidelines read: I.1 (every input of a pass is in its declaration),
// E.27 (each rule broken has a named error, reported by compile), R.3 and
// C.32 (the pointers in ImportedTexture, ImportedBuffer, ReadBack and
// RecordFunction are observers; the owner keeps the object alive through
// the frame), F.7 (resources are passed by reference), Con.5. Performance
// guidelines read: COPY.4 (declarations are plain structs copied by value),
// CDSA.26 (a pass names its resources by index, not by pointer).
namespace bw::graph {

inline constexpr std::uint16_t kInvalidHandle = 0xFFFFU;

struct TextureHandle {
  std::uint16_t index{kInvalidHandle};
  std::uint16_t frame{};
  [[nodiscard]] constexpr bool valid() const noexcept { return index != kInvalidHandle; }
  [[nodiscard]] friend constexpr bool operator==(TextureHandle, TextureHandle) noexcept = default;
};
struct BufferHandle {
  std::uint16_t index{kInvalidHandle};
  std::uint16_t frame{};
  [[nodiscard]] constexpr bool valid() const noexcept { return index != kInvalidHandle; }
  [[nodiscard]] friend constexpr bool operator==(BufferHandle, BufferHandle) noexcept = default;
};

struct FrameTextureDescription {
  rhi::Extent2 extent{};
  rhi::Format format{rhi::Format::Unknown};
  rhi::ClearValue clear_value{};
  std::string_view debug_name{};
};
struct FrameBufferDescription {
  std::uint64_t size{};
  std::string_view debug_name{};
};

enum class Contents : std::uint8_t { Keep, Overwrite, Clear };

struct TextureUse {
  TextureHandle texture{};
  Access access{Access::Sampled};
  Contents contents{Contents::Keep};
  std::uint8_t slot{};
};
struct BufferUse {
  BufferHandle buffer{};
  Access access{Access::Sampled};
};

struct ImportedTexture {
  rhi::Texture* texture{};
  rhi::Access entry{rhi::Access::Unknown};
  rhi::Access exit{rhi::Access::Unknown};
};
struct ImportedBuffer {
  rhi::Buffer* buffer{};
  rhi::Access entry{rhi::Access::Unknown};
  rhi::Access exit{rhi::Access::Unknown};
};

struct ReadBack {
  rhi::StagingBuffer* staging{};
  rhi::GpuFence* fence{};
  std::uint64_t staging_offset{};
};

class FrameGraph;

// What a record function receives: the list to record into, the pass's
// declared resources resolved to the RHI objects of this frame, and the
// attachment extent of a Raster pass (zero for the other kinds).
class RecordContext {
public:
  RecordContext(const FrameGraph& graph, rhi::CommandList& list, std::uint16_t pass,
                rhi::Extent2 extent) noexcept
      : graph_(&graph), list_(&list), pass_(pass), extent_(extent) {}

  [[nodiscard]] rhi::CommandList& list() const noexcept { return *list_; }
  // Null when this pass did not declare the handle (contract 5).
  [[nodiscard]] rhi::Texture* texture(TextureHandle handle) const noexcept;
  [[nodiscard]] rhi::Buffer* buffer(BufferHandle handle) const noexcept;
  [[nodiscard]] rhi::Extent2 extent() const noexcept { return extent_; }

private:
  const FrameGraph* graph_;
  rhi::CommandList* list_;
  std::uint16_t pass_;
  rhi::Extent2 extent_;
};

// A non-owning reference to a callable `void(RecordContext&)`. It binds an
// lvalue only, so the callable outlives the declaration by construction
// at the call site; the caller keeps it alive until execute returns.
// Copying a RecordFunction copies the reference, never wraps it. A
// PassDeclaration has no default state: it is built around its record
// function.
class RecordFunction {
public:
  template <class Callable>
    requires std::invocable<Callable&, RecordContext&> &&
                 (!std::same_as<std::remove_cvref_t<Callable>, RecordFunction>)
  RecordFunction(Callable& callable) noexcept // NOLINT(google-explicit-constructor)
      : object_(&callable), call_([](void* object, RecordContext& context) {
          (*static_cast<Callable*>(object))(context);
        }) {}

  void operator()(RecordContext& context) const { call_(object_, context); }

private:
  void* object_;
  void (*call_)(void*, RecordContext&);
};

struct PassDeclaration {
  std::string_view name{};
  PassKind kind{PassKind::Raster};
  std::span<const TextureUse> textures{};
  std::span<const BufferUse> buffers{};
  RecordFunction record;
};

} // namespace bw::graph

#endif
