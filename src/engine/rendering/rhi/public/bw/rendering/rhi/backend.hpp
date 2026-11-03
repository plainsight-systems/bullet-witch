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
//   work recorded since the last submit to the GPU and arranges for
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
//   meanings; set_draw_block is the one that can fail, with the frame's
//   transient budget, and reports it rather than growing the ring; a transition a backend's API
//   tracks itself may execute as no work, but it writes the tracked access of every resource in the
//   batch at end_transitions (transitions.hpp).
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
// device creation by this name.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHIGlobals.h:711-718: bSupportsBindless
// and the descriptor type groups; Engine/Source/Runtime/Apple/MetalRHI/Private/MetalRHI.cpp:490
// reads the bindless configuration at startup;
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalDevice.cpp:339-429 sets
// the feature flags from the device. Followed as a report with named
// required entries (docs/architecture/shaders.md, "Targets": Metal 3 on
// macOS 15 with argument buffers tier 2; Direct3D 12 shader model 6.4 and
// resource binding tier 2; Vulkan 1.3 with descriptor indexing).
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

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHIGlobals.h:267
// SupportsEfficientAsyncCompute, :174 SupportsTimestampRenderQueries,
// :114 SupportsMultithreading, :968 GRHISupportsAsyncPipelinePrecompile.
// Followed for the entries the architecture's scope table names. Omitted:
// the remaining several hundred globals, each tied to a feature the game
// does not reach.
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

// The execution side of command_list.hpp. One per backend.
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHIContext.h:254-567: IRHIComputeContext
// (RHIBeginTransitions and RHIEndTransitions :287-289, RHIWriteGPUFence
// :390-393, RHICopyToStagingBuffer :381-384, SetTrackedAccess :510-531,
// RHIBeginBreadcrumbGPU and RHIEndBreadcrumbGPU :369-372); :686-858
// IRHICommandContext (render queries :714-716, render pass :834-836, copies
// :842-844, RHISetGraphicsPipelineState :737). Followed, sized to the
// commands the list records. Omitted: the parallel render pass entries
// (:824-832), ray tracing, shader bundle, UAV overlap and multi-GPU
// transfer members (:268-339, :405-487), the per-stage shader parameter
// setters (binding is through descriptor indices in blocks), and the
// PSO fallback context (:877-914).
class Context {
public:
  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;
  virtual ~Context() = default;

  virtual void begin_transitions(const TransitionBatch& batch) = 0;
  virtual void end_transitions(const TransitionBatch& batch) = 0;
  virtual void begin_render_pass(const RenderPassDescription& pass) = 0;
  virtual void end_render_pass() = 0;
  virtual void set_graphics_pipeline(GraphicsPipeline& pipeline) = 0;
  virtual void set_compute_pipeline(ComputePipeline& pipeline) = 0;
  virtual void set_viewport(const Viewport& viewport) = 0;
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

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/DynamicRHI.h:198-997: FDynamicRHI. The
// entries kept, by the pin's line: Init 207 and Shutdown 213 (the
// constructor and destructor here), GetName 215 (Capabilities::name),
// RHICreateSamplerState 232, RHICreateRasterizerState 235,
// RHICreateDepthStencilState 238, RHICreateBlendState 241,
// RHICreateGraphicsPipelineState 395, RHICreateComputePipelineState 398,
// RHICreateBufferInitializer 425 and RHICreateTextureInitializer 481 (as
// create with initial data), RHICreateShaderResourceView 506 and
// RHICreateUnorderedAccessView 507, RHICreateGPUFence 292,
// RHICreateStagingBuffer 319, RHILockStagingBuffer 333 and
// RHIUnlockStagingBuffer 339, RHILockBuffer 427 and RHIUnlockBuffer 431,
// RHICreateRenderQuery 610 and RHIGetRenderQueryResult 620,
// RHIGetDefaultContext 799, RHISubmitCommandLists 881, RHIBlockUntilGPUIdle
// 697, RHIEndFrame 229 (reset_frame_resources), RHIProcessDeleteQueue 887
// (the device's queue). Omitted: the viewport, present and display entries
// (680-689, 630-632, 721-744; the next slice), the shader creation entries
// (247-287; artifacts are loaded by the shader system and handed to
// pipeline creation), RHICreateVertexDeclaration 244 (a value here),
// uniform buffer objects 414-416 (blocks are buffer ranges), texture
// references and replace 434, 423, 503, the async texture creation 498,
// the surface read entries 584-607 (staging readback), the finalize and
// close-chain entries 836-869 (one context, one submission), the transition
// create and release 301-305 (transitions are values), the transient
// allocator 312 (the render graph slice), ray tracing 922-970, descriptor
// ranges 518-523, debug label binding 575-577 (names are in descriptors),
// the suspend and resume entries 700-710, and RHITick 693.
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

  [[nodiscard]] virtual LockResult lock_buffer(Buffer& buffer, BufferRange range) = 0;
  [[nodiscard]] virtual LockError unlock_buffer(Buffer& buffer) = 0;
  [[nodiscard]] virtual StagingLockResult lock_staging(StagingBuffer& staging, GpuFence& fence,
                                                       WaitPolicy policy) = 0;
  [[nodiscard]] virtual StagingError unlock_staging(StagingBuffer& staging) = 0;
  [[nodiscard]] virtual QueryResult query_result(QueryPool& pool, std::uint32_t index,
                                                 WaitPolicy policy) = 0;

  [[nodiscard]] virtual Context& context() noexcept = 0;
  // Engine/Source/Runtime/Apple/MetalRHI/Private/MetalSubmission.cpp:427-455:
  // FinalizePayload assigns the next completion value; :457-587 commits the
  // command buffers and a signal command buffer that encodes the value on
  // the queue's event. Followed: the value is the frame number.
  virtual void submit_frame(std::uint64_t frame) = 0;
  [[nodiscard]] virtual std::uint64_t completed_frame() const noexcept = 0;
  // Precondition: frame <= the last submitted frame.
  virtual void wait_for_frame(std::uint64_t frame) = 0;
  virtual void wait_idle() = 0;
  // Engine/Source/Runtime/Apple/MetalRHI/Private/MetalSubmission.cpp:737-774:
  // the completed handler reads the command buffer's status and error.
  // Followed as a flag the device reads once per frame.
  [[nodiscard]] virtual bool take_gpu_fault() noexcept = 0;
  // Engine/Source/Runtime/Apple/MetalRHI/Private/MetalRHI.cpp:1226-1258:
  // RHIEndFrame garbage collects (heap compaction and the temporary
  // allocators' cleanup, MetalDevice.cpp:446-457). Followed with the
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
// ComputePipeline::debug_name).
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHIValidation.h:43: FValidationRHI derives
// from FDynamicRHI and wraps the real RHI;
// Engine/Source/Runtime/RHI/Private/RHIValidation.cpp:570-577: its
// constructor sets GRHIValidationEnabled (RHIValidationCommon.h:17-21); RHIValidation.cpp:830-1009
// builds the per-transition checks; :2114-2130 define the redundant-transition reason text, which
// the pin's BeginTransition (:2408-2542) never emits: reporting Redundant is this layer's
// departure. Followed otherwise. Omitted:
// the op queue replay across pipelines (RHIValidationCommon.h:572-592, 892-946), because one queue
// executes in order here; the uniform buffer lifetime checks (:594-604); the UAV overlap tracking.
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
