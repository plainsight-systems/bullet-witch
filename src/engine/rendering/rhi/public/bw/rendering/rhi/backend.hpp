#ifndef BW_RENDERING_RHI_BACKEND_HPP
#define BW_RENDERING_RHI_BACKEND_HPP
#include "bw/rendering/rhi/binding_structure.hpp"
#include "bw/rendering/rhi/build.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/descriptor_allocator.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/queries.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/transitions.hpp"
#include "bw/rendering/rhi/viewport.hpp"
#include "bw/rendering/shaders/root_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

// Responsibility: what a backend implements (docs/architecture/rhi.md,
// "Backends" and invariant 10): the capability report, every creation entry
// point, the one execution context, submission with a completion value per
// frame, the waits, and the frame hook. Metal, Vulkan and Direct3D 12 each
// derive Backend and Context; the device (device.hpp) owns one Backend and
// is the only caller. The validation layer is a Backend that wraps another
// and exists only when BW_RHI_DEBUG is 1.
//
// Contract.
//   Capabilities are filled at creation and never change. The two required
//   capabilities, typed descriptor arrays and the shader floor, are checked
//   by the device; a backend that cannot report both true fails its own
//   creation with RequiredCapabilityMissing and the capability's name, and
//   the device fails startup with it (invariant 10). The optional ones are
//   reported false until a backend proves them.
//   Creation: the device validates every description first
//   (resources.hpp, pipeline_states.hpp) and allocates descriptor slots, so
//   a backend receives descriptions it may assume valid and slots it writes;
//   it may still fail with OutOfMemory, ShaderLibraryRejected,
//   EntryPointNotFound or BackendRejected, never by returning a different
//   object than asked for. Every object a backend creates is constructed
//   against options.delete_queue so release goes through the queue.
//   Execution: context() is the one context; it is driven by one thread at
//   a time, the device's submission thread. submit_frame(frame) hands the
//   work recorded since the last submit to the GPU, presents every viewport
//   back buffer acquired in the frame on that work's command buffer and
//   releases it (viewport.hpp, invariants 2 and 3), and arranges for
//   completed_frame() to reach `frame` when that work is done; the value is
//   monotonic. wait_for_frame blocks without spinning until
//   completed_frame() >= frame, with the precondition that frame has been
//   submitted (the device derives it from required_completion); wait_idle
//   until every submitted frame has completed. take_gpu_fault() is true
//   once when work submitted since the last call completed with an API
//   error (a GPU fault: the results of that work are undefined), and
//   clears the flag; the device reads it at end_frame
//   (EndFrameResult::gpu_fault). reset_frame_resources(completed) is called by the device
//   after each submit with the completed value: the backend resets the
//   transient rings of frames at or below it and compacts its pools; it
//   never frees an RHI object (that is the delete queue's).
//   Context operations are the commands of command_list.hpp with the same
//   meanings; two can fail: set_draw_block, with the frame's transient
//   budget, which it reports rather than growing the ring, and
//   begin_render_pass, when a back buffer among its targets cannot be
//   acquired (viewport.hpp, invariant 4); a transition a backend's API
//   tracks itself may execute as no work, but it writes the tracked access of every resource in the
//   batch at end_transitions (transitions.hpp).
//   Viewports: create_viewport receives a description the device validated
//   (validate_viewport, and the surface kind against surface_kind) and
//   creates the surface's state and the back buffer texture;
//   resize_viewport is called after the device waited idle and replaces
//   the back buffer; set_vsync applies the flag to the surface. Each
//   backend takes one surface kind, reported in Capabilities.
//
// Core guidelines read: C.121 and C.122 (Backend and Context are pure
// abstract classes with virtual destructors; no data), C.67 (copy
// suppressed), I.11 (ownership of a created backend passes in a unique_ptr;
// resources pass in Refs), I.1 (options are explicit). Performance
// guidelines read: GPU.7 (a completion value per frame; waits name a frame,
// never the device), MEM.6 (the backend receives its queue and budgets
// through options, never a global).
namespace bw::rhi {

enum class BackendKind : std::uint8_t { Metal, Vulkan, Direct3D12 };

// The capabilities every backend must report true. A missing one fails
// device creation by this name. The backend sets them from the device at
// creation (docs/architecture/shaders.md, "Targets": Metal 3 on macOS 15
// with argument buffers tier 2; Direct3D 12 shader model 6.4 and resource
// binding tier 2; Vulkan 1.3 with descriptor indexing).
enum class RequiredCapability : std::uint8_t {
  TypedDescriptorArrays,
  ShaderFloor,
};
[[nodiscard]] constexpr std::string_view name(RequiredCapability capability) noexcept {
  switch (capability) {
  case RequiredCapability::TypedDescriptorArrays:
    return "TypedDescriptorArrays";
  case RequiredCapability::ShaderFloor:
    return "ShaderFloor";
  }
  return {};
}

// The reported entries are the ones the architecture's scope table names.
inline constexpr std::size_t kDeviceNameCapacity = 127;
struct Capabilities {
  BackendKind backend{BackendKind::Metal};
  std::array<char, kDeviceNameCapacity + 1> device_name{};
  std::uint8_t device_name_length{};
  // Required.
  bool typed_descriptor_arrays{};
  bool shader_floor{};
  // Reported, off until a backend proves it.
  bool async_compute{};
  bool mesh_shaders{};
  bool ray_tracing{};
  bool timestamp_queries{};
  // Limits.
  TextureLimits texture_limits{};
  std::uint32_t uniform_buffer_alignment{};
  // The native surface this backend's viewports render into
  // (viewport.hpp; platform/window/window.hpp): MetalLayer for Metal.
  platform::NativeSurfaceKind surface_kind{platform::NativeSurfaceKind::None};
  // The budgets the backend was created with (BackendOptions), reported
  // back so a caller reads one record.
  std::uint32_t frames_in_flight{};
  DescriptorCapacities descriptors{};
  std::uint64_t transient_bytes_per_frame{};
  [[nodiscard]] std::string_view name() const noexcept {
    return {device_name.data(), device_name_length};
  }
};

enum class DeviceError : std::uint8_t {
  None,
  // The backend is not compiled into this build (the Metal backend on a
  // non-Apple host, the others until their slices land).
  BackendUnavailable,
  NoAdapter,
  // detail() names the RequiredCapability.
  RequiredCapabilityMissing,
  DescriptorCapacityInvalid,
  FramesInFlightInvalid,
  TransientBudgetInvalid,
  // The API's device, queue or initial allocations failed, or a build-time
  // assumption of the backend did not hold on this device; detail() names
  // which.
  BackendInitializationFailed,
};

// What the device hands a backend at creation. The pointers outlive the
// backend.
struct BackendOptions {
  DeferredDeleteQueue* delete_queue{};
  // The device's descriptor pools: the slots the device passes to the
  // create_* calls below come from them, and a View's destructor returns
  // its slot there (resources.hpp, Views).
  DescriptorAllocators* slot_pools{};
  const shaders::RootLayout* root_layout{};
  const BindingStructure* binding_structure{};
  DescriptorCapacities descriptors{};
  std::uint32_t frames_in_flight{};
  std::uint64_t transient_bytes_per_frame{};
};

// The execution side of command_list.hpp. One per backend, sized to the
// commands the list records. There are no per-stage shader parameter
// setters: binding is through descriptor indices in blocks.
class Context {
public:
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  virtual ~Context() = default;

  virtual void begin_transitions(const TransitionBatch& batch) = 0;
  virtual void end_transitions(const TransitionBatch& batch) = 0;
  // Acquires a back buffer among the targets when it is the frame's first
  // pass that binds it; BackBufferUnavailable or BackBufferSizeMismatch
  // opens no pass (viewport.hpp, invariant 4).
  [[nodiscard]] virtual ExecuteError begin_render_pass(const RenderPassDescription& pass) = 0;
  virtual void end_render_pass() = 0;
  virtual void set_graphics_pipeline(GraphicsPipeline& pipeline) = 0;
  virtual void set_compute_pipeline(ComputePipeline& pipeline) = 0;
  virtual void set_viewport(const ViewportRect& viewport) = 0;
  virtual void set_scissor(const ScissorRect& rect) = 0;
  virtual void set_stencil_reference(std::uint32_t reference) = 0;
  virtual void set_vertex_buffer(std::uint32_t slot, Buffer& buffer, std::uint64_t offset) = 0;
  virtual void set_index_buffer(Buffer& buffer, IndexFormat format, std::uint64_t offset) = 0;
  virtual void set_static_block(std::uint32_t slot, Buffer& buffer, std::uint64_t offset) = 0;
  // Copies the bytes into the frame's transient ring and binds the range.
  // The ring never grows: TransientBudgetExhausted or DrawBlockTooLarge is
  // returned and nothing is bound.
  [[nodiscard]] virtual ExecuteError set_draw_block(std::span<const std::byte> bytes,
                                                    shaders::LayoutHash layout) = 0;
  virtual void set_root_constants(std::uint32_t first_word,
                                  std::span<const std::uint32_t> words) = 0;
  virtual void draw(const DrawArguments& arguments) = 0;
  virtual void draw_indexed(const DrawIndexedArguments& arguments) = 0;
  virtual void dispatch(const DispatchArguments& arguments) = 0;
  virtual void copy_buffer(Buffer& source, Buffer& destination, const BufferCopy& copy) = 0;
  virtual void copy_texture(Texture& source, Texture& destination,
                            const TextureCopyRegion& region) = 0;
  virtual void copy_buffer_to_texture(Buffer& source, const BufferTextureRegion& region,
                                      Texture& destination) = 0;
  virtual void copy_texture_to_staging(Texture& source, std::uint8_t mip, std::uint16_t slice,
                                       StagingBuffer& destination,
                                       std::uint64_t destination_offset) = 0;
  virtual void copy_buffer_to_staging(Buffer& source, StagingBuffer& destination,
                                      const BufferCopy& copy) = 0;
  virtual void write_fence(GpuFence& fence) = 0;
  virtual void begin_query(QueryPool& pool, std::uint32_t index) = 0;
  virtual void end_query(QueryPool& pool, std::uint32_t index) = 0;
  virtual void begin_marker(std::string_view name) = 0;
  virtual void end_marker() = 0;

protected:
  Context() = default;
};

// Initialization and shutdown are the constructor and destructor; buffers
// and textures are created with their initial data. There is no shader
// creation entry: artifacts are loaded by the shader system and handed to
// pipeline creation. The vertex layout and transitions are values, blocks
// are buffer ranges, readback goes through staging buffers, debug names are
// in the descriptions, and there is one context and one submission. The
// delete queue is the device's.
class Backend {
public:
  Backend(const Backend&) = delete;
  Backend& operator=(const Backend&) = delete;
  virtual ~Backend() = default;

  [[nodiscard]] virtual const Capabilities& capabilities() const noexcept = 0;
  [[nodiscard]] virtual FormatCapability format_capabilities(Format format) const noexcept = 0;

  [[nodiscard]] virtual BufferResult create_buffer(const BufferDescription& description,
                                                   std::span<const std::byte> initial_data) = 0;
  [[nodiscard]] virtual TextureResult
  create_texture(const TextureDescription& description,
                 std::span<const SubresourceData> initial_data) = 0;
  [[nodiscard]] virtual ShaderResourceViewResult
  create_shader_resource_view(Ref<Buffer> buffer, const BufferViewDescription& view,
                              DescriptorSlot slot) = 0;
  [[nodiscard]] virtual ShaderResourceViewResult
  create_shader_resource_view(Ref<Texture> texture, const TextureViewDescription& view,
                              DescriptorSlot slot) = 0;
  [[nodiscard]] virtual UnorderedAccessViewResult
  create_unordered_access_view(Ref<Buffer> buffer, const BufferViewDescription& view,
                               DescriptorSlot slot) = 0;
  [[nodiscard]] virtual UnorderedAccessViewResult
  create_unordered_access_view(Ref<Texture> texture, const TextureViewDescription& view,
                               DescriptorSlot slot) = 0;
  [[nodiscard]] virtual SamplerResult create_sampler(const SamplerDescription& description,
                                                     DescriptorSlot slot) = 0;
  [[nodiscard]] virtual RasterizerStateResult
  create_rasterizer_state(const RasterizerDescription& description) = 0;
  [[nodiscard]] virtual DepthStencilStateResult
  create_depth_stencil_state(const DepthStencilDescription& description) = 0;
  [[nodiscard]] virtual BlendStateResult
  create_blend_state(const BlendDescription& description) = 0;
  [[nodiscard]] virtual GraphicsPipelineResult
  create_graphics_pipeline(const GraphicsPipelineDescription& description,
                           const PipelineBinding& binding) = 0;
  [[nodiscard]] virtual ComputePipelineResult
  create_compute_pipeline(const ComputePipelineDescription& description,
                          const PipelineBinding& binding) = 0;
  [[nodiscard]] virtual GpuFenceResult create_fence(std::string_view debug_name) = 0;
  [[nodiscard]] virtual StagingBufferResult create_staging_buffer(std::uint64_t size,
                                                                  std::string_view debug_name) = 0;
  [[nodiscard]] virtual QueryPoolResult
  create_query_pool(const QueryPoolDescription& description) = 0;
  // Viewports (viewport.hpp). Preconditions: the description passed
  // validate_viewport and its kind is surface_kind; resize_viewport runs
  // after wait_idle with a size validate_resize accepted.
  [[nodiscard]] virtual ViewportResult create_viewport(const ViewportDescription& description) = 0;
  [[nodiscard]] virtual Status<ViewportError> resize_viewport(Viewport& viewport,
                                                              Extent2 pixel_size) = 0;
  virtual void set_vsync(Viewport& viewport, bool vsync) = 0;

  [[nodiscard]] virtual LockResult lock_buffer(Buffer& buffer, BufferRange range) = 0;
  [[nodiscard]] virtual LockError unlock_buffer(Buffer& buffer) = 0;
  [[nodiscard]] virtual StagingLockResult lock_staging(StagingBuffer& staging, GpuFence& fence,
                                                       WaitPolicy policy) = 0;
  [[nodiscard]] virtual StagingError unlock_staging(StagingBuffer& staging) = 0;
  [[nodiscard]] virtual QueryResult query_result(QueryPool& pool, std::uint32_t index,
                                                 WaitPolicy policy) = 0;

  [[nodiscard]] virtual Context& context() noexcept = 0;
  // The completion value the submitted work signals is the frame number.
  // Presents and releases the back buffers acquired in the frame.
  virtual void submit_frame(std::uint64_t frame) = 0;
  [[nodiscard]] virtual std::uint64_t completed_frame() const noexcept = 0;
  // Precondition: frame <= the last submitted frame.
  virtual void wait_for_frame(std::uint64_t frame) = 0;
  virtual void wait_idle() = 0;
  // Set from the status of completed work; the device reads it once per
  // frame.
  [[nodiscard]] virtual bool take_gpu_fault() noexcept = 0;
  // Resets the transient allocations and compacts the pools, with the
  // completed frame as the gate.
  virtual void reset_frame_resources(std::uint64_t completed_frame) = 0;

protected:
  Backend() = default;
};

using BackendResult = Result<std::unique_ptr<Backend>, DeviceError>;

// The one backend of this build for `kind`: Metal on Apple hosts; the
// others return BackendUnavailable until their slices land. Precondition:
// options validated by the device.
[[nodiscard]] BackendResult create_backend(BackendKind kind, const BackendOptions& options);

#if BW_RHI_DEBUG
// The validation layer: a Backend that wraps another and checks every
// declared transition and every use against transitions.hpp, every render
// pass against validate_render_pass, every pipeline against the pass it is
// set in (render_target_layout), every draw block against the pipeline's
// draw block hash, and the render pass and marker nesting rules of
// command_list.hpp. A failure is reported once through the reporter with
// the code, the resource's debug name and the command's name, and the
// command is then forwarded; the layer never changes what the backend
// sees. A transition batch is reported once per failing transition, each
// with that transition's resource name. A redundant transition is reported
// with Redundant and forwarded. A pipeline's name in a failure is its
// description's debug name (GraphicsPipeline::description,
// ComputePipeline::debug_name). One queue executes in order, so the checks
// need no replay of operations across pipelines.
enum class ValidationCode : std::uint8_t {
  None,
  // transition carries the TransitionError.
  Transition,
  Redundant,
  RenderPassInvalid,
  CommandOutsideRenderPass,
  CommandInsideRenderPass,
  NestedRenderPass,
  EndWithoutBegin,
  PipelineTargetsMismatch,
  DrawBlockLayoutMismatch,
  NoPipelineBound,
  MarkerUnbalanced,
  // A render pass still open at submit_frame (command_list.hpp).
  RenderPassOpenAtSubmit,
  QueryOutsidePass,
  QueryIndexOutOfRange,
  // begin_query on an AbsoluteTime pool (queries.hpp).
  QueryKindMismatch,
  // command_list.hpp, Blocks.
  StaticSlotOutOfRange,
  RootConstantsOutOfRange,
  DrawBlockNotSet,
  StaticSlotNotSet,
  // viewport.hpp, invariant 2: a copy from a back buffer no render pass of
  // the frame has bound; checked before the copy source's access, so an
  // unacquired back buffer is reported by this code whatever its access.
  BackBufferNotAcquired,
  // viewport.hpp, invariant 3: at submit_frame, an acquired back buffer
  // whose tracked access is not Present.
  BackBufferNotPresentable,
};
struct ValidationFailure {
  ValidationCode code{ValidationCode::None};
  TransitionError transition{TransitionError::None};
  std::string_view resource_name{};
  std::string_view command{};
};
using ValidationReporter = void (*)(const ValidationFailure& failure, void* user) noexcept;

// Takes ownership of inner; the returned backend forwards to it.
[[nodiscard]] std::unique_ptr<Backend>
wrap_with_validation(std::unique_ptr<Backend> inner, ValidationReporter reporter, void* user);
// The default reporter: writes one diagnostic line naming the code, the
// resource and the command, then aborts. A test installs its own.
void report_validation_failure_and_abort(const ValidationFailure& failure, void* user) noexcept;
#endif

} // namespace bw::rhi
#endif
