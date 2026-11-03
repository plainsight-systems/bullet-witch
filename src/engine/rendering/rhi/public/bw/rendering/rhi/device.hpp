#ifndef BW_RENDERING_RHI_DEVICE_HPP
#define BW_RENDERING_RHI_DEVICE_HPP
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/binding_structure.hpp"
#include "bw/rendering/rhi/build.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/descriptor_allocator.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/pipeline_cache.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/queries.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"
#include "bw/rendering/rhi/synchronization.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

// Responsibility: the RHI the renderer talks to (docs/architecture/rhi.md,
// "Position": one backend selected at startup that lives for the process;
// invariants 2, 4, 5, 6, 7, 8 and 10). A Device owns one Backend, the
// capability check, the descriptor index allocators, the deferred delete
// queue, the frames-in-flight bookkeeping, the shared state objects, the
// pipeline description cache and the binding structure. It validates every
// description, then calls the backend; it executes command lists on the
// backend's context; it is the one frame boundary.
//
// Contract.
//   create: validates the description (validate_device_description:
//   descriptor capacities, frames in flight 1..kMaxFramesInFlight,
//   transient budget non-zero), creates the
//   backend, checks the two required capabilities and fails with
//   RequiredCapabilityMissing naming the first missing one, derives the
//   binding structure from the engine root layout, and in debug builds
//   wraps the backend with the validation layer unless validation is off.
//   Creation functions: thread-safe (one device mutex); each validates with
//   the pure functions of its header and returns that code, else the
//   backend's. A view's slot is allocated before the backend call and
//   returned to the pool when the view is destroyed (by its destructor,
//   resources.hpp; the device holds its mutex whenever the delete queue
//   destroys, so the pools see one caller at a time). State objects and
//   samplers are shared: two equal descriptions return the same object;
//   they live as long as the device.
//   Pipelines: graphics_pipeline and compute_pipeline look the key up in the
//   description cache; on a miss, with PipelineMissPolicy::CreateOnMiss the
//   pipeline is created and inserted, with Error the call returns
//   PipelineError::CacheMiss; precompile creates every description given,
//   in order, and stops at the first failure with its code and the
//   description's debug name. The default policy is CreateOnMiss with
//   BW_RHI_DEBUG 1 and Error with 0 (the shipping rule of the architecture).
//   execute(list): replays the list on the backend's context on the calling
//   thread; the caller is the one submission thread (invariant 1); the
//   replay's first failure is returned and the frame is then not drawn.
//   end_frame: the one frame boundary (invariant 8). It submits the frame
//   with the frame number as completion value, blocks on the oldest frame
//   when frames_in_flight are queued (lifetime.hpp, required_completion)
//   and reports that it waited, reads the completed frame, destroys the
//   delete queue's entries at or below it, tells the backend to reset the
//   transient resources of those frames, reads the backend's GPU fault
//   flag, and advances the frame number. No begin-frame call exists.
//   wait_idle: blocks until every submitted frame has completed and runs the
//   delete queue for them. The destructor waits idle, destroys the shared
//   objects and the cache, drains the queue, and destroys the backend.
//   Locks, staging and queries forward to the backend after the checks
//   their headers state.
//
// Core guidelines read: C.67 and I.11 (the device is not copyable and is
// created into a unique_ptr), I.1 (every budget and policy is in the
// description), E.27 (every path returns a named code), CP.3 (one mutex
// serializes creation; execution is single-threaded by contract), F.7
// (resources are taken by reference). Performance guidelines read: GPU.7
// (bounded frames in flight; the wait names the oldest frame), MEM.4 (one
// flip point per frame), MEM.6 (the backend receives its budgets),
// GDSA.17 (pipelines by content key), MEM.2 (descriptor slots from fixed
// pools with explicit exhaustion).
namespace bw::rhi {

enum class PipelineMissPolicy : std::uint8_t { CreateOnMiss, Error };

inline constexpr std::uint32_t kDefaultReadDescriptors = 65536;
inline constexpr std::uint64_t kDefaultTransientBytesPerFrame = 16ULL * 1024ULL * 1024ULL;

struct DeviceDescription {
  BackendKind backend{BackendKind::Metal};
  DescriptorCapacities descriptors{kDefaultReadDescriptors, shaders::kMaxSamplerDescriptors,
                                   shaders::kStorageDescriptors};
  std::uint32_t frames_in_flight{kDefaultFramesInFlight};
  std::uint64_t transient_bytes_per_frame{kDefaultTransientBytesPerFrame};
#if BW_RHI_DEBUG
  PipelineMissPolicy miss_policy{PipelineMissPolicy::CreateOnMiss};
  bool validation{true};
  // Null means report_validation_failure_and_abort.
  ValidationReporter reporter{};
  void* reporter_user{};
#else
  PipelineMissPolicy miss_policy{PipelineMissPolicy::Error};
#endif
};

// The description checks of Device::create, the first that applies in this
// order: DescriptorCapacityInvalid (descriptor_capacities_valid),
// FramesInFlightInvalid (outside 1..kMaxFramesInFlight),
// TransientBudgetInvalid (zero).
[[nodiscard]] constexpr DeviceError
validate_device_description(const DeviceDescription& d) noexcept {
  if (!descriptor_capacities_valid(d.descriptors)) {
    return DeviceError::DescriptorCapacityInvalid;
  }
  if (d.frames_in_flight == 0U || d.frames_in_flight > kMaxFramesInFlight) {
    return DeviceError::FramesInFlightInvalid;
  }
  if (d.transient_bytes_per_frame == 0U) {
    return DeviceError::TransientBudgetInvalid;
  }
  return DeviceError::None;
}

struct EndFrameResult {
  // The frame just submitted.
  std::uint64_t submitted{};
  // The completed frame after any wait.
  std::uint64_t completed{};
  // True when the CPU blocked on the oldest frame in flight.
  bool waited{};
  // Entries the delete queue destroyed.
  std::size_t destroyed{};
  // True when the backend reports that submitted work completed with an
  // API error since the last end_frame (backend.hpp, take_gpu_fault); the
  // results of that work are undefined and the caller decides.
  bool gpu_fault{};
};

class Device;
using DeviceResult = Result<std::unique_ptr<Device>, DeviceError>;

// The device is an object the host owns, not a global. end_frame is the
// frame boundary: the delete queue runs, then the backend ends the frame with
// the incremented frame number.
class Device final {
public:
  [[nodiscard]] static DeviceResult create(const DeviceDescription& description);
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  ~Device();

  [[nodiscard]] const Capabilities& capabilities() const noexcept;
  [[nodiscard]] FormatCapability format_capabilities(Format format) const noexcept;
  [[nodiscard]] const BindingStructure& binding_structure() const noexcept { return binding_; }
  [[nodiscard]] const DeviceDescription& description() const noexcept { return description_; }

  // Resources (resources.hpp).
  [[nodiscard]] BufferResult create_buffer(const BufferDescription& description,
                                           std::span<const std::byte> initial_data = {});
  [[nodiscard]] TextureResult create_texture(const TextureDescription& description,
                                             std::span<const SubresourceData> initial_data = {});
  [[nodiscard]] ShaderResourceViewResult
  create_shader_resource_view(Ref<Buffer> buffer, const BufferViewDescription& view);
  [[nodiscard]] ShaderResourceViewResult
  create_shader_resource_view(Ref<Texture> texture, const TextureViewDescription& view);
  [[nodiscard]] UnorderedAccessViewResult
  create_unordered_access_view(Ref<Buffer> buffer, const BufferViewDescription& view);
  [[nodiscard]] UnorderedAccessViewResult
  create_unordered_access_view(Ref<Texture> texture, const TextureViewDescription& view);
  [[nodiscard]] LockResult lock_buffer(Buffer& buffer, BufferRange range);
  [[nodiscard]] LockError unlock_buffer(Buffer& buffer);

  // Shared state objects (pipeline_states.hpp).
  [[nodiscard]] SamplerResult sampler(const SamplerDescription& description);
  [[nodiscard]] RasterizerStateResult rasterizer_state(const RasterizerDescription& description);
  [[nodiscard]] DepthStencilStateResult
  depth_stencil_state(const DepthStencilDescription& description);
  [[nodiscard]] BlendStateResult blend_state(const BlendDescription& description);

  // Pipelines (pipeline_states.hpp, pipeline_cache.hpp).
  [[nodiscard]] GraphicsPipelineResult
  graphics_pipeline(const GraphicsPipelineDescription& description);
  [[nodiscard]] ComputePipelineResult
  compute_pipeline(const ComputePipelineDescription& description);
  [[nodiscard]] Status<PipelineError>
  precompile(std::span<const GraphicsPipelineDescription> graphics,
             std::span<const ComputePipelineDescription> compute);
  [[nodiscard]] const PipelineDescriptionCache& pipeline_cache() const noexcept { return cache_; }

  // Synchronization and queries (synchronization.hpp, queries.hpp).
  [[nodiscard]] GpuFenceResult create_fence(std::string_view debug_name);
  [[nodiscard]] StagingBufferResult create_staging_buffer(std::uint64_t size,
                                                          std::string_view debug_name);
  [[nodiscard]] StagingLockResult lock_staging(StagingBuffer& staging, GpuFence& fence,
                                               WaitPolicy policy);
  [[nodiscard]] StagingError unlock_staging(StagingBuffer& staging);
  [[nodiscard]] QueryPoolResult create_query_pool(const QueryPoolDescription& description);
  [[nodiscard]] QueryResult query_result(QueryPool& pool, std::uint32_t index, WaitPolicy policy);

  // Execution and the frame boundary.
  // Replays the list on the backend's context; returns the first failure
  // the replay reports (command_list.hpp, ExecuteError).
  [[nodiscard]] Status<ExecuteError> execute(const CommandList& list);
  // Rejects by name a wait that could never return (synchronization.hpp,
  // FenceState), else blocks until the fence is signaled.
  [[nodiscard]] Status<StagingError> wait_fence(GpuFence& fence);
  [[nodiscard]] EndFrameResult end_frame();
  void wait_idle();
  // The frame being recorded, from 1.
  [[nodiscard]] std::uint64_t frame() const noexcept { return frame_; }
  [[nodiscard]] std::uint64_t completed_frame() const noexcept;
  [[nodiscard]] std::uint32_t frames_in_flight() const noexcept {
    return description_.frames_in_flight;
  }
  [[nodiscard]] const DeferredDeleteQueue& delete_queue() const noexcept { return queue_; }
  [[nodiscard]] const DescriptorAllocators& descriptors() const noexcept { return descriptors_; }

private:
  Device(const DeviceDescription& description, std::unique_ptr<Backend> backend,
         const BindingStructure& binding);

  DeviceDescription description_;
  DeferredDeleteQueue queue_{};
  DescriptorAllocators descriptors_;
  BindingStructure binding_;
  std::unique_ptr<Backend> backend_;
  PipelineDescriptionCache cache_{};
  std::vector<Ref<Sampler>> samplers_{};
  std::vector<Ref<RasterizerState>> rasterizer_states_{};
  std::vector<Ref<DepthStencilState>> depth_stencil_states_{};
  std::vector<Ref<BlendState>> blend_states_{};
  std::mutex mutex_{};
  std::uint64_t frame_{1};
};

} // namespace bw::rhi
#endif
