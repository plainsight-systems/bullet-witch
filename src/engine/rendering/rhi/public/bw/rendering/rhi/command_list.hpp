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
//   block (first + n <= shaders::kRootConstantWords, else
//   RootConstantsOutOfRange). Bindings do not survive end_frame: a draw or
//   dispatch whose pipeline uses the draw block, or a static slot in its
//   mask, that has not been set since the frame began is a violation
//   (DrawBlockNotSet, StaticSlotNotSet). Descriptor indices are not bound
//   here: they are values in the blocks.
//   Transitions: begin_transitions and end_transitions take a batch; the
//   combined transition is begin then end in one command. The arrays are
//   copied.
//   Markers: begin_marker and end_marker nest and must balance within the
//   list. With BW_RHI_DEBUG 0 they record nothing and compile to nothing.
//   replay(Context&) issues every command in order with its copied
//   arguments and leaves the list intact; it returns the first failure a
//   command reports and issues nothing after it (ExecuteError: the frame's
//   transient ring is exhausted, or one draw block is larger than the ring;
//   the ring never grows). The detail names the command. reset() empties
//   the list. A list is replayed at most once per recording.
//
// Algorithm: a vector of bytes holding (kind, size) headers followed by the
// command's plain struct and any trailing array bytes, 8-byte aligned;
// replay reads headers in order. Appending is amortized O(1); replay O(n).
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
};

struct Viewport {
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

struct Offset3 {
  std::uint32_t x{};
  std::uint32_t y{};
  std::uint32_t z{};
};
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHICommandList.h:181: FRHICopyTextureInfo
// (size, source and destination positions, mips, slices). Followed.
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

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHICommandList.h:347-351: FRHICommandBase,
// an intrusive node with ExecuteAndDestruct; :566-575 AllocCommand links a
// node allocated from the list's memory stack; :453-1380 FRHICommandListBase;
// :2493-3585 FRHIComputeCommandList and :3590-4328 FRHICommandList, the
// recording interfaces; Engine/Source/Runtime/RHI/Private/RHICommandList.cpp:517-548
// Execute walks the chain and runs each command against the context
// (Engine/Source/Runtime/RHI/Public/RHICommandListCommandExecutes.inl:7-14).
// Followed as a byte stream with one record per command. Omitted: the
// bypass mode that executes directly into the context
// (RHICommandList.inl:27-34), the immediate list, sub-lists for parallel
// render passes (RHICommandList.h:5035-5045), lambda commands (:353-410),
// dispatch prerequisites and the RHI thread fence (:509-519, :751), the
// GPU mask, the shader parameter batching (:996-1010) and the pending
// upload tracking: the game has one submission path and binds through
// descriptor indices.
class CommandList {
public:
  CommandList();
  CommandList(const CommandList&) = delete;
  CommandList& operator=(const CommandList&) = delete;
  ~CommandList();

  // Transitions. RHICommandList.h:2739-2778 BeginTransitions and
  // EndTransitions; RHICommandList.cpp:2128-2132 Transition records the
  // combined form then the tracked access.
  void begin_transitions(const TransitionBatch& batch);
  void end_transitions(const TransitionBatch& batch);
  void transition(const TransitionBatch& batch);

  // Render pass. RHICommandList.h:4030-4060 BeginRenderPass (checks not
  // inside a pass, validates the info, records, caches the targets);
  // :4062-4083 EndRenderPass. The name is copied.
  void begin_render_pass(const RenderPassDescription& pass);
  void end_render_pass();
  [[nodiscard]] bool inside_render_pass() const noexcept { return inside_pass_; }

  // Pipeline and fixed state. RHICommandList.h:3883-3894
  // SetGraphicsPipelineState(state, stencil ref); :2667-2677 compute.
  void set_graphics_pipeline(GraphicsPipeline& pipeline);
  void set_compute_pipeline(ComputePipeline& pipeline);
  void set_viewport(const Viewport& viewport);
  void set_scissor(const ScissorRect& rect);
  void set_stencil_reference(std::uint32_t reference);

  // Geometry.
  void set_vertex_buffer(std::uint32_t slot, Buffer& buffer, std::uint64_t offset);
  void set_index_buffer(Buffer& buffer, IndexFormat format, std::uint64_t offset);

  // Root layout binding (root_layout.hpp). The pin binds uniform buffers
  // per stage and slot (RHIContext.h, RHISetStaticUniformBuffers at
  // Engine/Source/Runtime/RHI/Public/RHIContext.h:254-567); here one call
  // per slot for every stage, since the layout is one.
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

  // Copies. RHICommandList.h:3996-4008 CopyTexture (source and destination
  // differ, outside a render pass); :4105-4122 CopyBufferRegion;
  // :3153-3161 CopyToStagingBuffer.
  void copy_buffer(Buffer& source, Buffer& destination, const BufferCopy& copy);
  void copy_texture(Texture& source, Texture& destination, const TextureCopyRegion& region);
  void copy_buffer_to_texture(Buffer& source, const BufferTextureRegion& region,
                              Texture& destination);
  void copy_texture_to_staging(Texture& source, std::uint8_t mip, std::uint16_t slice,
                               StagingBuffer& destination, std::uint64_t destination_offset);
  void copy_buffer_to_staging(Buffer& source, StagingBuffer& destination, const BufferCopy& copy);

  // Fences and queries. RHICommandList.h:3163-3166 WriteGPUFence;
  // :4020-4028 BeginRenderQuery and EndRenderQuery.
  void write_fence(GpuFence& fence);
  void begin_query(QueryPool& pool, std::uint32_t index);
  void end_query(QueryPool& pool, std::uint32_t index);

  // Markers. RHICommandList.h:3091-3147 BeginBreadcrumbGPU and
  // EndBreadcrumbGPU reach the context's RHIBeginBreadcrumbGPU and
  // RHIEndBreadcrumbGPU (RHIContext.h:369-372). Followed as a name per
  // scope. Omitted: the breadcrumb tree, its allocator and crash output
  // (RHIBreadcrumbs.h:136-310).
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
