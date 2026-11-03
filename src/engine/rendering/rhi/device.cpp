#include "bw/rendering/rhi/device.hpp"

#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/root_layout.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace bw::rhi {

namespace {

[[nodiscard]] shaders::ShaderTarget shader_target(BackendKind kind) noexcept {
  switch (kind) {
  case BackendKind::Metal:
    return shaders::ShaderTarget::Metal;
  case BackendKind::Vulkan:
    return shaders::ShaderTarget::Vulkan;
  case BackendKind::Direct3D12:
    return shaders::ShaderTarget::Direct3D12;
  }
  return shaders::ShaderTarget::Metal;
}

} // namespace

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Private/DynamicRHI.cpp:290-335: RHIInit selects
// the platform's one dynamic RHI and initializes it, failing startup when
// none is available. Followed, with the device description validated first
// and the two required capabilities checked by name after the backend
// reports them. Omitted: the InitNullRHI branch for a process that can
// never render (:304-308), because the game has one real backend per
// platform and no null RHI (a missing backend is BackendUnavailable); and
// RHIDetectAndWarnOfBadDrivers (:315-333), a driver blacklist warning that
// belongs to an editor or launcher, not to the game's device.
//
// Order: BackendOptions hands the backend pointers to the device's delete
// queue, descriptor pools and binding structure, which must outlive the backend (backend.hpp),
// so the Device is constructed first, with no backend and the binding
// structure already derived (a pure function of the engine root layout,
// binding_structure.hpp), and the backend is attached once it has passed the
// capability check. Every early return destroys the Device and any backend
// before returning the error, so a failed create leaves no object (E.4).
DeviceResult Device::create(const DeviceDescription& description) {
  const DeviceError invalid = validate_device_description(description);
  if (invalid != DeviceError::None) {
    return DeviceResult::failure(invalid);
  }
  const shaders::RootLayout& layout = shaders::engine_root_layout();
  std::unique_ptr<Device> device(
      new Device(description, std::unique_ptr<Backend>{}, derive_binding_structure(layout)));
  const BackendOptions options{.delete_queue = &device->queue_,
                               .slot_pools = &device->descriptors_,
                               .root_layout = &layout,
                               .binding_structure = &device->binding_,
                               .descriptors = description.descriptors,
                               .frames_in_flight = description.frames_in_flight,
                               .transient_bytes_per_frame = description.transient_bytes_per_frame};
  BackendResult created = create_backend(description.backend, options);
  if (!created.ok()) {
    return DeviceResult::failure(created.error(), created.detail());
  }
  std::unique_ptr<Backend> backend = std::move(created.value());
  const Capabilities& capabilities = backend->capabilities();
  if (!capabilities.typed_descriptor_arrays) {
    return DeviceResult::failure(DeviceError::RequiredCapabilityMissing,
                                 name(RequiredCapability::TypedDescriptorArrays));
  }
  if (!capabilities.shader_floor) {
    return DeviceResult::failure(DeviceError::RequiredCapabilityMissing,
                                 name(RequiredCapability::ShaderFloor));
  }
#if BW_RHI_DEBUG
  if (description.validation) {
    const ValidationReporter reporter = description.reporter != nullptr
                                            ? description.reporter
                                            : &report_validation_failure_and_abort;
    backend = wrap_with_validation(std::move(backend), reporter, description.reporter_user);
  }
#endif
  device->backend_ = std::move(backend);
  return DeviceResult::success(std::move(device));
}

Device::Device(const DeviceDescription& description, std::unique_ptr<Backend> backend,
               const BindingStructure& binding)
    : description_(description), descriptors_(description.descriptors), binding_(binding),
      backend_(std::move(backend)) {}

// The contract's order: wait idle, destroy the shared objects and the
// cache, drain the queue, destroy the backend. backend_ is null only for a
// Device whose create failed before the backend was attached; nothing was
// created through it then, so there is nothing to wait for. The drain runs
// under mutex_ like every destroy of the queue (device.hpp:42-47): it runs
// view destructors, which free slots in descriptors_.
Device::~Device() {
  if (backend_ != nullptr) {
    backend_->wait_idle();
  }
  {
    const std::scoped_lock lock(mutex_);
    for (const Ref<Sampler>& sampler : samplers_) {
      descriptors_.free(sampler->slot());
    }
    samplers_.clear();
    rasterizer_states_.clear();
    depth_stencil_states_.clear();
    blend_states_.clear();
    cache_.clear();
    static_cast<void>(queue_.drain());
  }
  backend_.reset();
}

const Capabilities& Device::capabilities() const noexcept { return backend_->capabilities(); }

FormatCapability Device::format_capabilities(Format format) const noexcept {
  return backend_->format_capabilities(format);
}

std::uint64_t Device::completed_frame() const noexcept { return backend_->completed_frame(); }

// ---- Resources -------------------------------------------------------------

BufferResult Device::create_buffer(const BufferDescription& description,
                                   std::span<const std::byte> initial_data) {
  const std::scoped_lock lock(mutex_);
  const ResourceError error = validate_buffer(description);
  if (error != ResourceError::None) {
    return BufferResult::failure(error, description.debug_name);
  }
  const ResourceError data_error = validate_buffer_initial_data(description, initial_data);
  if (data_error != ResourceError::None) {
    return BufferResult::failure(data_error, description.debug_name);
  }
  return backend_->create_buffer(description, initial_data);
}

TextureResult Device::create_texture(const TextureDescription& description,
                                     std::span<const SubresourceData> initial_data) {
  const std::scoped_lock lock(mutex_);
  const ResourceError error = validate_texture(description, backend_->capabilities().texture_limits,
                                               backend_->format_capabilities(description.format));
  if (error != ResourceError::None) {
    return TextureResult::failure(error, description.debug_name);
  }
  const ResourceError data_error = validate_initial_data(description, initial_data);
  if (data_error != ResourceError::None) {
    return TextureResult::failure(data_error, description.debug_name);
  }
  return backend_->create_texture(description, initial_data);
}

// Views: validate with resources.hpp, take a slot from the pool the view's
// array belongs to, then call the backend; a backend failure returns the slot
// and the backend's code unchanged.
//
// On success the slot belongs to the view; the view's destructor returns it
// to descriptors_ (resources.hpp, Views), and views are destroyed only by
// the delete queue under mutex_ (device.hpp:42-47), so the pools see one
// caller at a time.
//
// A null resource is NullResource; the view description is validated
// before a slot is taken (ResourceError order), so a bad description never
// consumes a slot.
namespace {
template <class ViewResult, class ResourceType, class ViewDescription, class Create>
ViewResult create_view_with_slot(DescriptorAllocators& descriptors, Ref<ResourceType> resource,
                                 const ViewDescription& view, ResourceError error,
                                 shaders::DescriptorArray array, Create create) {
  if (error != ResourceError::None) {
    return ViewResult::failure(error, resource->debug_name());
  }
  const std::optional<DescriptorSlot> slot = descriptors.allocate(array);
  if (!slot.has_value()) {
    return ViewResult::failure(ResourceError::DescriptorPoolExhausted, resource->debug_name());
  }
  ViewResult result = create(std::move(resource), view, *slot);
  if (!result.ok()) {
    descriptors.free(*slot);
  }
  return result;
}
} // namespace

ShaderResourceViewResult Device::create_shader_resource_view(Ref<Buffer> buffer,
                                                             const BufferViewDescription& view) {
  const std::scoped_lock lock(mutex_);
  if (!buffer) {
    return ShaderResourceViewResult::failure(ResourceError::NullResource, "null resource");
  }
  const ResourceError error = validate_buffer_view(buffer->description(), view, false);
  return create_view_with_slot<ShaderResourceViewResult>(
      descriptors_, std::move(buffer), view, error, shaders::DescriptorArray::Buffer,
      [this](Ref<Buffer> b, const BufferViewDescription& v, DescriptorSlot s) {
        return backend_->create_shader_resource_view(std::move(b), v, s);
      });
}

ShaderResourceViewResult Device::create_shader_resource_view(Ref<Texture> texture,
                                                             const TextureViewDescription& view) {
  const std::scoped_lock lock(mutex_);
  if (!texture) {
    return ShaderResourceViewResult::failure(ResourceError::NullResource, "null resource");
  }
  const ResourceError error = validate_texture_view(texture->description(), view, false);
  return create_view_with_slot<ShaderResourceViewResult>(
      descriptors_, std::move(texture), view, error, array_for(view.dimension, false),
      [this](Ref<Texture> t, const TextureViewDescription& v, DescriptorSlot s) {
        return backend_->create_shader_resource_view(std::move(t), v, s);
      });
}

UnorderedAccessViewResult Device::create_unordered_access_view(Ref<Buffer> buffer,
                                                               const BufferViewDescription& view) {
  const std::scoped_lock lock(mutex_);
  if (!buffer) {
    return UnorderedAccessViewResult::failure(ResourceError::NullResource, "null resource");
  }
  const ResourceError error = validate_buffer_view(buffer->description(), view, true);
  return create_view_with_slot<UnorderedAccessViewResult>(
      descriptors_, std::move(buffer), view, error, shaders::DescriptorArray::StorageBuffer,
      [this](Ref<Buffer> b, const BufferViewDescription& v, DescriptorSlot s) {
        return backend_->create_unordered_access_view(std::move(b), v, s);
      });
}

UnorderedAccessViewResult Device::create_unordered_access_view(Ref<Texture> texture,
                                                               const TextureViewDescription& view) {
  const std::scoped_lock lock(mutex_);
  if (!texture) {
    return UnorderedAccessViewResult::failure(ResourceError::NullResource, "null resource");
  }
  const ResourceError error = validate_texture_view(texture->description(), view, true);
  return create_view_with_slot<UnorderedAccessViewResult>(
      descriptors_, std::move(texture), view, error, array_for(view.dimension, true),
      [this](Ref<Texture> t, const TextureViewDescription& v, DescriptorSlot s) {
        return backend_->create_unordered_access_view(std::move(t), v, s);
      });
}

// resources.hpp, validate_lock, then the backend. The Device keeps no
// per-buffer lock state, so AlreadyLocked and NotLocked are reported by the
// backend that holds the mapping (device.hpp: locks forward to the backend
// after the checks their headers state).
LockResult Device::lock_buffer(Buffer& buffer, BufferRange range) {
  const std::scoped_lock lock(mutex_);
  const LockError error = validate_lock(buffer.description(), range);
  if (error != LockError::None) {
    return LockResult::failure(error, buffer.debug_name());
  }
  return backend_->lock_buffer(buffer, range);
}

LockError Device::unlock_buffer(Buffer& buffer) {
  const std::scoped_lock lock(mutex_);
  if (!has_usage(buffer.description().usage, BufferUsage::Upload)) {
    return LockError::NotUploadBuffer;
  }
  return backend_->unlock_buffer(buffer);
}

// ---- Shared state objects --------------------------------------------------

// Two equal descriptions return the same object (pipeline_states.hpp); the
// objects live as long as the device. The sampler takes its slot of the
// sampler table before the backend call and returns it if the call fails.
SamplerResult Device::sampler(const SamplerDescription& description) {
  const std::scoped_lock lock(mutex_);
  for (const Ref<Sampler>& existing : samplers_) {
    if (existing->description() == description) {
      return SamplerResult::success(existing);
    }
  }
  const std::optional<DescriptorSlot> slot =
      descriptors_.allocate(shaders::DescriptorArray::Sampler);
  if (!slot.has_value()) {
    return SamplerResult::failure(ResourceError::DescriptorPoolExhausted);
  }
  SamplerResult result = backend_->create_sampler(description, *slot);
  if (!result.ok()) {
    descriptors_.free(*slot);
    return result;
  }
  samplers_.push_back(result.value());
  return result;
}

namespace {
template <class Result, class State, class Description, class Create>
Result shared_state(std::vector<Ref<State>>& states, const Description& description,
                    Create create) {
  for (const Ref<State>& existing : states) {
    if (existing->description() == description) {
      return Result::success(existing);
    }
  }
  Result result = create(description);
  if (result.ok()) {
    states.push_back(result.value());
  }
  return result;
}
} // namespace

RasterizerStateResult Device::rasterizer_state(const RasterizerDescription& description) {
  const std::scoped_lock lock(mutex_);
  return shared_state<RasterizerStateResult>(
      rasterizer_states_, description,
      [this](const RasterizerDescription& d) { return backend_->create_rasterizer_state(d); });
}

DepthStencilStateResult Device::depth_stencil_state(const DepthStencilDescription& description) {
  const std::scoped_lock lock(mutex_);
  return shared_state<DepthStencilStateResult>(
      depth_stencil_states_, description,
      [this](const DepthStencilDescription& d) { return backend_->create_depth_stencil_state(d); });
}

BlendStateResult Device::blend_state(const BlendDescription& description) {
  const std::scoped_lock lock(mutex_);
  return shared_state<BlendStateResult>(
      blend_states_, description,
      [this](const BlendDescription& d) { return backend_->create_blend_state(d); });
}

// ---- Pipelines -------------------------------------------------------------

namespace {

// The device's checks of a graphics description: the pure checks of
// pipeline_states.hpp (validate_graphics_pipeline), then the ones that need
// the device's state, in the header's order: WrongTarget against this
// device's ShaderTarget, FormatNotRenderable and SampleCountUnsupported
// against the capability report. VertexLayoutUnsupported is listed among
// the device's checks (pipeline_states.hpp:406) but no capability reports
// whether a backend takes a vertex layout; the backend returns it
// (pipeline_states.hpp:36-37), with ShaderLibraryRejected,
// EntryPointNotFound and BackendRejected.
[[nodiscard]] PipelineError validate_graphics(const GraphicsPipelineDescription& d,
                                              shaders::ShaderTarget target,
                                              const Backend& backend) noexcept {
  const PipelineError pure = validate_graphics_pipeline(d);
  if (pure != PipelineError::None) {
    return pure;
  }
  if (d.vertex_shader->header.target != target ||
      (d.pixel_shader != nullptr && d.pixel_shader->header.target != target)) {
    return PipelineError::WrongTarget;
  }
  for (std::uint32_t i = 0; i < d.targets.color_count; ++i) {
    if (!has_capability(backend.format_capabilities(d.targets.color_formats[i]),
                        FormatCapability::RenderTarget)) {
      return PipelineError::FormatNotRenderable;
    }
  }
  if (d.targets.depth_stencil_format != Format::Unknown &&
      !has_capability(backend.format_capabilities(d.targets.depth_stencil_format),
                      FormatCapability::DepthStencil)) {
    return PipelineError::FormatNotRenderable;
  }
  const std::uint32_t samples = d.targets.sample_count;
  if (samples == 0U || (samples & (samples - 1U)) != 0U ||
      samples > backend.capabilities().texture_limits.max_sample_count) {
    return PipelineError::SampleCountUnsupported;
  }
  return PipelineError::None;
}

[[nodiscard]] PipelineError validate_compute(const ComputePipelineDescription& d,
                                             shaders::ShaderTarget target) noexcept {
  const PipelineError pure = validate_compute_pipeline(d);
  if (pure != PipelineError::None) {
    return pure;
  }
  if (d.compute_shader->header.target != target) {
    return PipelineError::WrongTarget;
  }
  return PipelineError::None;
}

[[nodiscard]] PipelineBinding graphics_binding(const GraphicsPipelineDescription& d) noexcept {
  const shaders::BindingUse vertex = shaders::derive_binding_use(d.vertex_shader->reflection);
  if (d.pixel_shader == nullptr) {
    return derive_pipeline_binding(vertex, nullptr);
  }
  const shaders::BindingUse pixel = shaders::derive_binding_use(d.pixel_shader->reflection);
  return derive_pipeline_binding(vertex, &pixel);
}

} // namespace

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Private/PipelineStateCache.cpp:1808-1875: Find,
// then create and Add on a miss. Followed with the content key and the miss
// policy of device.hpp. Called with the device mutex held.
namespace {

GraphicsPipelineResult find_or_create(const GraphicsPipelineDescription& description,
                                      PipelineMissPolicy policy, shaders::ShaderTarget target,
                                      Backend& backend, PipelineDescriptionCache& cache) {
  const PipelineError error = validate_graphics(description, target, backend);
  if (error != PipelineError::None) {
    return GraphicsPipelineResult::failure(error, description.debug_name);
  }
  const PipelineKey key = graphics_pipeline_key(description);
  Ref<GraphicsPipeline> cached = cache.find_graphics(key);
  if (cached) {
    return GraphicsPipelineResult::success(std::move(cached));
  }
  if (policy == PipelineMissPolicy::Error) {
    return GraphicsPipelineResult::failure(PipelineError::CacheMiss, description.debug_name);
  }
  GraphicsPipelineResult created =
      backend.create_graphics_pipeline(description, graphics_binding(description));
  if (created.ok()) {
    static_cast<void>(cache.insert(key, created.value()));
  }
  return created;
}

ComputePipelineResult find_or_create(const ComputePipelineDescription& description,
                                     PipelineMissPolicy policy, shaders::ShaderTarget target,
                                     Backend& backend, PipelineDescriptionCache& cache) {
  const PipelineError error = validate_compute(description, target);
  if (error != PipelineError::None) {
    return ComputePipelineResult::failure(error, description.debug_name);
  }
  const PipelineKey key = compute_pipeline_key(description);
  Ref<ComputePipeline> cached = cache.find_compute(key);
  if (cached) {
    return ComputePipelineResult::success(std::move(cached));
  }
  if (policy == PipelineMissPolicy::Error) {
    return ComputePipelineResult::failure(PipelineError::CacheMiss, description.debug_name);
  }
  const PipelineBinding binding = derive_pipeline_binding(
      shaders::derive_binding_use(description.compute_shader->reflection), nullptr);
  ComputePipelineResult created = backend.create_compute_pipeline(description, binding);
  if (created.ok()) {
    static_cast<void>(cache.insert(key, created.value()));
  }
  return created;
}

} // namespace

GraphicsPipelineResult Device::graphics_pipeline(const GraphicsPipelineDescription& description) {
  const std::scoped_lock lock(mutex_);
  return find_or_create(description, description_.miss_policy, shader_target(description_.backend),
                        *backend_, cache_);
}

ComputePipelineResult Device::compute_pipeline(const ComputePipelineDescription& description) {
  const std::scoped_lock lock(mutex_);
  return find_or_create(description, description_.miss_policy, shader_target(description_.backend),
                        *backend_, cache_);
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/PipelineStateCache.h:241-244: precache
// takes complete initializers. Followed: every description is created, in
// order, whatever the miss policy, and the first failure stops it with its
// code and the description's debug name.
Status<PipelineError> Device::precompile(std::span<const GraphicsPipelineDescription> graphics,
                                         std::span<const ComputePipelineDescription> compute) {
  const std::scoped_lock lock(mutex_);
  const shaders::ShaderTarget target = shader_target(description_.backend);
  for (const GraphicsPipelineDescription& description : graphics) {
    const GraphicsPipelineResult result =
        find_or_create(description, PipelineMissPolicy::CreateOnMiss, target, *backend_, cache_);
    if (!result.ok()) {
      return Status<PipelineError>{result.error(), description.debug_name};
    }
  }
  for (const ComputePipelineDescription& description : compute) {
    const ComputePipelineResult result =
        find_or_create(description, PipelineMissPolicy::CreateOnMiss, target, *backend_, cache_);
    if (!result.ok()) {
      return Status<PipelineError>{result.error(), description.debug_name};
    }
  }
  return Status<PipelineError>{};
}

// ---- Synchronization and queries -------------------------------------------

GpuFenceResult Device::create_fence(std::string_view debug_name) {
  const std::scoped_lock lock(mutex_);
  return backend_->create_fence(debug_name);
}

StagingBufferResult Device::create_staging_buffer(std::uint64_t size, std::string_view debug_name) {
  const std::scoped_lock lock(mutex_);
  return backend_->create_staging_buffer(size, debug_name);
}

StagingLockResult Device::lock_staging(StagingBuffer& staging, GpuFence& fence, WaitPolicy policy) {
  const StagingError error = fence_wait_error(fence.state());
  if (error != StagingError::None) {
    return StagingLockResult::failure(error, fence.debug_name());
  }
  const std::scoped_lock lock(mutex_);
  return backend_->lock_staging(staging, fence, policy);
}

StagingError Device::unlock_staging(StagingBuffer& staging) {
  const std::scoped_lock lock(mutex_);
  return backend_->unlock_staging(staging);
}

// queries.hpp: validate_query_pool, then the capability check that is the
// device's (AbsoluteTime needs timestamp_queries), then the backend.
QueryPoolResult Device::create_query_pool(const QueryPoolDescription& description) {
  const std::scoped_lock lock(mutex_);
  const QueryError error = validate_query_pool(description);
  if (error != QueryError::None) {
    return QueryPoolResult::failure(error, description.debug_name);
  }
  if (description.kind == QueryKind::AbsoluteTime && !backend_->capabilities().timestamp_queries) {
    return QueryPoolResult::failure(QueryError::KindUnsupported, description.debug_name);
  }
  return backend_->create_query_pool(description);
}

QueryResult Device::query_result(QueryPool& pool, std::uint32_t index, WaitPolicy policy) {
  if (index >= pool.count()) {
    return QueryResult::failure(QueryError::IndexOutOfRange, pool.debug_name());
  }
  const std::scoped_lock lock(mutex_);
  return backend_->query_result(pool, index, policy);
}

// ---- Execution and the frame boundary --------------------------------------

// The caller is the one submission thread (invariant 1): replay is not
// serialized with the creation mutex.
Status<ExecuteError> Device::execute(const CommandList& list) {
  return list.replay(backend_->context());
}

Status<StagingError> Device::wait_fence(GpuFence& fence) {
  const StagingError error = fence_wait_error(fence.state());
  if (error != StagingError::None) {
    return Status<StagingError>{error, fence.debug_name()};
  }
  fence.wait();
  return Status<StagingError>{};
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Private/RHICommandList.cpp:1414-1464:
// Submit_Close processes the delete queue, then calls RHIEndFrame with the
// frame number and increments it. Followed in device.hpp's order: submit the
// frame with its number as completion value; block on the oldest frame when
// frames_in_flight are queued (Engine/Source/Runtime/Apple/MetalRHI/Private/MetalViewport.cpp:1106,
// the semaphore wait);
// read the completed frame; destroy the delete queue's entries at or below
// it; reset the backend's transient resources of those frames; read the
// backend's GPU fault flag (backend.hpp, take_gpu_fault); advance the frame
// number.
EndFrameResult Device::end_frame() {
  EndFrameResult result{};
  result.submitted = frame_;
  backend_->submit_frame(frame_);
  const std::optional<std::uint64_t> wait = required_completion(
      FrameProgress{frame_ + 1U, backend_->completed_frame()}, description_.frames_in_flight);
  if (wait.has_value()) {
    backend_->wait_for_frame(*wait);
    result.waited = true;
  }
  result.completed = backend_->completed_frame();
  {
    // View destructors free slots in descriptors_ (device.hpp:42-47).
    const std::scoped_lock lock(mutex_);
    result.destroyed = queue_.destroy_completed(result.completed);
  }
  backend_->reset_frame_resources(result.completed);
  result.gpu_fault = backend_->take_gpu_fault();
  ++frame_;
  queue_.set_current_frame(frame_);
  return result;
}

void Device::wait_idle() {
  backend_->wait_idle();
  const std::uint64_t completed = backend_->completed_frame();
  // View destructors free slots in descriptors_ (device.hpp:42-47).
  const std::scoped_lock lock(mutex_);
  static_cast<void>(queue_.destroy_completed(completed));
}

} // namespace bw::rhi
