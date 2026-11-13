#ifndef BW_RENDERING_RHI_COMMAND_LIST_HPP
#define BW_RENDERING_RHI_COMMAND_LIST_HPP
#include "bw/rendering/rhi/build.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/queries.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/transitions.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Responsibility: command recording (docs/architecture/rhi.md, invariant 1:
// recording never touches a graphics API; command lists record; contexts
// execute them on a backend; a context is driven by one thread at a time;
// submission is one serialized path). A CommandList is a byte stream of
// commands and their copied arguments; replay walks it once and calls one
// Context function per command (backend.hpp). Nothing here allocates an API
// object or knows which backend will run it.
//
// Contract.
//   Recording: every call appends one command. The list copies every
//   argument it is given, including draw block bytes, root constant words,
//   transition arrays and marker names, so the caller's storage may die
//   after the call. Resource pointers are copied as observers: the list
//   does not add references, and a list must be executed (device.hpp,
//   Device::execute) within the frame it was recorded in, before that
//   frame's end_frame; the deferred delete queue then keeps every resource
//   the frame named alive until the GPU is done (lifetime.hpp).
//   Threads: one thread records a given list at a time; different lists may
//   be recorded on different threads at once. Replay happens on the thread
//   that calls Device::execute, which is the one submission thread.
//   Back buffers (viewport.hpp): a render pass whose color target is a
//   viewport's back buffer acquires it from the surface at replay when it
//   is the first such pass of the frame; the acquisition fails by name
//   (ExecuteError::BackBufferUnavailable, BackBufferSizeMismatch), the
//   replay stops there and the frame is not drawn. The back buffer is
//   presented at end_frame, not by a command.
//   Render passes: begin_render_pass and end_render_pass nest nothing;
//   draws, the graphics pipeline, viewport, scissor, stencil reference,
//   vertex and index buffer and occlusion query calls are legal only inside
//   a pass; dispatch, the compute pipeline, copies, transitions, fences and
//   the AbsoluteTime end_query only outside one; block and root constant
//   calls are legal in both places and bind to the pass's graphics pipeline
//   inside a pass and to the compute pipeline outside one. A pass still open
//   at end_frame is a violation (RenderPassOpenAtSubmit). The validation
//   layer reports a violation by name; without it the backend's behavior is
//   undefined.
//   Blocks: set_static_block binds a buffer range as static slot `slot`
//   (slot < shaders::kMaxStaticBlocks, else StaticSlotOutOfRange) until the
//   frame ends or the slot is set again; set_draw_block copies the bytes
//   into the list and at replay into the frame's transient ring (invariant
//   7), and binds that range as the draw block until the frame ends or the
//   next set_draw_block; the layout hash travels with the bytes so the
//   validation layer can compare it with the pipeline's draw block hash.
//   set_root_constants writes words [first, first + n) of the root constant
//   block (first + n <= shaders::kCallerRootConstantWords, else
//   RootConstantsOutOfRange); the block's last word,
//   shaders::kDrawIndexRootConstantWord, is the backend's and no caller
//   writes it. Bindings do not survive end_frame: a draw or
//   dispatch whose pipeline uses the draw block, or a static slot in its
//   mask, that has not been set since the frame began is a violation
//   (DrawBlockNotSet, StaticSlotNotSet). Descriptor indices are not bound
//   here: they are values in the blocks.
//   Indirect work (docs/architecture/rhi.md, the scope table and the
//   Backends table): three operations whose arguments are read from a
//   buffer on the device, which GPU work may have written.
//   draw_indexed_indirect draws once with the DrawIndexedArguments at
//   `offset` of `arguments`; dispatch_indirect dispatches once with the
//   DispatchArguments there. draw_indexed_indirect_count is the counted
//   multi-draw: with n the 32-bit count at `count_offset` of `counts`, it
//   draws min(n, maximum) times, draw i with the IndirectDrawRecord at
//   record_offset + i * stride of `records`, in index order, and reads
//   nothing of a record at or past that number. The CPU never reads the
//   count. Each draw is an indexed draw with the bound pipeline, index
//   buffer, blocks and root constants, as draw_indexed is. In a record,
//   instance_count is 1 and base_vertex and first_instance are 0
//   (bw/rendering/geometry/vertex_streams.hpp: the base vertex travels in
//   a block), and draw_index is i: whoever writes the records writes it,
//   because one API sets a draw's constant from its record and reads it
//   there. A shader reads the index of its draw within the multi-draw
//   through the bindless include; in every other draw that index is 0.
//   On the targets whose shaders read it from the root constant block it
//   is the word shaders::kDrawIndexRootConstantWord, which the backend
//   writes: i for draw i of a multi-draw, 0 for every other draw.
//   Preconditions: every offset and the stride are multiples of 4; stride
//   is at least kIndirectDrawRecordBytes; maximum is at least 1 and at
//   most Capabilities::max_multi_draw_count; records + maximum * stride
//   and the count lie within their buffers; each buffer was created with
//   BufferUsage::IndirectArguments and is in Access::IndirectArguments.
//   The two draws are legal only inside a pass, dispatch_indirect only
//   outside one. Of these the validation layer checks the placement, the
//   pipeline, the bindings and the buffers' access; the offsets, stride,
//   maximum, spans and creation usage are the caller's and no
//   ValidationCode names them.
//   Transitions: begin_transitions and end_transitions take a batch; the
//   combined transition is begin then end in one command. The arrays are
//   copied.
//   Markers: begin_marker and end_marker nest and must balance within the
//   list. With BW_RHI_DEBUG 0 they record nothing and compile to nothing.
//   replay(Context&) issues every command in order with its copied
//   arguments and leaves the list intact; it returns the first failure a
//   command reports and issues nothing after it (ExecuteError: the frame's
//   transient ring is exhausted, or one draw block is larger than the ring;
//   the ring never grows; a back buffer could not be acquired). The detail
//   names the command. reset() empties
//   the list. A list is replayed at most once per recording.
//
// Algorithm: a vector of bytes holding (kind, size) headers followed by the
// command's plain struct and any trailing array bytes, 8-byte aligned;
// replay reads headers in order. Appending is amortized O(1); replay O(n).
//
// The counted multi-draw's record carries the draw's index before the
// arguments because Direct3D 12 applies a record's arguments in order, so a
// constant the draw reads precedes the draw; Vulkan numbers the draws
// itself and Metal's indirect commands are encoded from the records.
// On Direct3D 12 the count is the minimum of the maximum and a 32-bit value
// in the count buffer, offsets are 4-byte aligned, and both buffers are in
// the indirect argument state; a record's arguments are tightly packed in
// order with an arbitrary stride between records, and the indexed draw
// arguments are 20 bytes. On Vulkan the stride is a multiple of 4 and at
// least the command's size, the count is 32-bit, offsets are multiples of
// 4, the count is at most maxDrawIndirectCount, and a shader's draw index
// is zero in a direct draw and the draw's number in an indirect one. On
// Metal an execution range is read from a buffer, at most 16,384 commands
// on one GPU family.
// Every draw of the renderer is indexed and every list has a GPU-written
// count, so there is no non-indexed indirect draw and no uncounted
// multi-draw. One index buffer and one binding serve a multi-draw, so a
// record carries no vertex buffer, index buffer or descriptor arguments.
//
// Core guidelines read: I.1 (every argument is explicit and copied; no
// state is read from the device at record time), C.67 (the list is not
// copyable: it holds a frame's worth of observers), F.7 and C.32 (resources
// are passed by reference and kept as observers, by the lifetime rule
// above), R.14 (spans in). Performance guidelines read: MEM.1 (the list is
// a per-frame arena that is reset, not freed per command), CACHE.3 (one
// contiguous byte stream, no per-command heap node), COPY.4 was read:
// arguments are POD structs copied by value.
namespace bw::rhi {

class Context;

// What replay can fail with. The transient ring has the budget
// DeviceDescription::transient_bytes_per_frame and never grows; a frame
// whose replay fails is not drawn and the caller decides what to do.
enum class ExecuteError : std::uint8_t {
  None,
  TransientBudgetExhausted,
  // One draw block alone exceeds the ring.
  DrawBlockTooLarge,
  // begin_render_pass on a viewport's back buffer: the surface gave no
  // drawable (viewport.hpp, invariant 4). The detail names the pass.
  BackBufferUnavailable,
  // The drawable's size differs from the back buffer's; the host resizes
  // the viewport (viewport.hpp, invariant 5).
  BackBufferSizeMismatch,
};

struct ViewportRect {
  float x{};
  float y{};
  float width{};
  float height{};
  float min_depth{};
  float max_depth{1.0F};
};
struct ScissorRect {
  std::int32_t x{};
  std::int32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};
};
enum class IndexFormat : std::uint8_t { Uint16, Uint32 };

struct DrawArguments {
  std::uint32_t vertex_count{};
  std::uint32_t instance_count{1};
  std::uint32_t first_vertex{};
  std::uint32_t first_instance{};
};
struct DrawIndexedArguments {
  std::uint32_t index_count{};
  std::uint32_t instance_count{1};
  std::uint32_t first_index{};
  std::int32_t base_vertex{};
  std::uint32_t first_instance{};
};
struct DispatchArguments {
  std::uint32_t x{1};
  std::uint32_t y{1};
  std::uint32_t z{1};
};

// One record of a counted multi-draw: the draw's index within the
// multi-draw, then its arguments. 24 bytes; a record buffer's stride may be
// larger, and what follows these bytes is the writer's.
struct IndirectDrawRecord {
  std::uint32_t draw_index{};
  DrawIndexedArguments arguments{};
};
inline constexpr std::uint32_t kIndirectDrawRecordBytes = 24;
static_assert(sizeof(IndirectDrawRecord) == kIndirectDrawRecordBytes);

// Where a counted multi-draw's records and count are.
struct MultiDrawArguments {
  std::uint64_t record_offset{};
  std::uint32_t stride{kIndirectDrawRecordBytes};
  std::uint64_t count_offset{};
  std::uint32_t maximum{};
};

struct Offset3 {
  std::uint32_t x{};
  std::uint32_t y{};
  std::uint32_t z{};
};
struct TextureCopyRegion {
  std::uint8_t source_mip{};
  std::uint16_t source_slice{};
  Offset3 source_origin{};
  std::uint8_t destination_mip{};
  std::uint16_t destination_slice{};
  Offset3 destination_origin{};
  // Zero extent means the whole source mip.
  Extent3 extent{};
};
// A buffer region holding one texture subresource in the staging layout
// (synchronization.hpp, staging_layout) or a caller-given pitch.
struct BufferTextureRegion {
  std::uint64_t buffer_offset{};
  std::uint32_t row_pitch{};
  std::uint32_t slice_pitch{};
  std::uint8_t mip{};
  std::uint16_t slice{};
};

class CommandList {
public:
  CommandList();
  CommandList(const CommandList&) = delete;
  CommandList& operator=(const CommandList&) = delete;
  ~CommandList();

  // Transitions. transition records the combined form.
  void begin_transitions(const TransitionBatch& batch);
  void end_transitions(const TransitionBatch& batch);
  void transition(const TransitionBatch& batch);

  // Render pass. The name is copied.
  void begin_render_pass(const RenderPassDescription& pass);
  void end_render_pass();
  [[nodiscard]] bool inside_render_pass() const noexcept { return inside_pass_; }

  // Pipeline and fixed state.
  void set_graphics_pipeline(GraphicsPipeline& pipeline);
  void set_compute_pipeline(ComputePipeline& pipeline);
  void set_viewport(const ViewportRect& viewport);
  void set_scissor(const ScissorRect& rect);
  void set_stencil_reference(std::uint32_t reference);

  // Geometry.
  void set_vertex_buffer(std::uint32_t slot, Buffer& buffer, std::uint64_t offset);
  void set_index_buffer(Buffer& buffer, IndexFormat format, std::uint64_t offset);

  // Root layout binding (root_layout.hpp): one call per slot binds it for
  // every stage, since the layout is one.
  void set_static_block(std::uint32_t slot, Buffer& buffer, std::uint64_t offset);
  void set_draw_block(std::span<const std::byte> bytes, shaders::LayoutHash layout);
  template <shaders::ParameterBlock Block> void set_draw_block(const Block& block) {
    set_draw_block(std::as_bytes(std::span<const Block, 1>{&block, 1}),
                   shaders::BlockLayoutOf<Block>::value.hash);
  }
  void set_root_constants(std::uint32_t first_word, std::span<const std::uint32_t> words);

  // Work.
  void draw(const DrawArguments& arguments);
  void draw_indexed(const DrawIndexedArguments& arguments);
  void dispatch(const DispatchArguments& arguments);
  // Indirect work: arguments the GPU wrote.
  void draw_indexed_indirect(Buffer& arguments, std::uint64_t offset);
  void draw_indexed_indirect_count(Buffer& records, Buffer& counts,
                                   const MultiDrawArguments& arguments);
  void dispatch_indirect(Buffer& arguments, std::uint64_t offset);

  // Copies.
  void copy_buffer(Buffer& source, Buffer& destination, const BufferCopy& copy);
  void copy_texture(Texture& source, Texture& destination, const TextureCopyRegion& region);
  void copy_buffer_to_texture(Buffer& source, const BufferTextureRegion& region,
                              Texture& destination);
  void copy_texture_to_staging(Texture& source, std::uint8_t mip, std::uint16_t slice,
                               StagingBuffer& destination, std::uint64_t destination_offset);
  void copy_buffer_to_staging(Buffer& source, StagingBuffer& destination, const BufferCopy& copy);

  // Fences and queries.
  void write_fence(GpuFence& fence);
  void begin_query(QueryPool& pool, std::uint32_t index);
  void end_query(QueryPool& pool, std::uint32_t index);

  // Markers: a name per scope, passed through to the context.
#if BW_RHI_DEBUG
  void begin_marker(std::string_view name);
  void end_marker();
#else
  void begin_marker(std::string_view) noexcept {}
  void end_marker() noexcept {}
#endif

  // Replay and reuse.
  [[nodiscard]] Status<ExecuteError> replay(Context& context) const;
  void reset() noexcept;
  [[nodiscard]] std::size_t command_count() const noexcept { return command_count_; }
  [[nodiscard]] std::size_t byte_size() const noexcept { return bytes_.size(); }

private:
  std::vector<std::byte> bytes_{};
  std::size_t command_count_{};
  std::uint32_t marker_depth_{};
  bool inside_pass_{};
};

} // namespace bw::rhi
#endif
