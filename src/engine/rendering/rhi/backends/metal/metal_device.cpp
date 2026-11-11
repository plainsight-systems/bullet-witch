#include "metal_device.hpp"

#include "bw/memory/alignment.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/synchronization.hpp"

#include "metal_backend.hpp"
#include "metal_formats.hpp"
#include "metal_objects.hpp"
#include "metal_viewport.hpp"

#include <dispatch/dispatch.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bw::rhi::metal {

namespace {

// 1000000 ms bounds each blocking call here, repeated until the value is
// reached.
constexpr std::uint64_t kWaitMilliseconds = 1000000;

void wait_for_event(MTL::SharedEvent& event, std::uint64_t value) {
  while (event.signaledValue() < value) {
    static_cast<void>(event.waitUntilSignaledValue(value, kWaitMilliseconds));
  }
}

// On the Apple7 and later families the maximum 2D and cube texture width and
// height are 16384, a 3D texture's width, height and depth 2048, and a texture
// array's length 2048. The device is checked for the Apple7 family before this
// is read.
constexpr TextureLimits kApple7TextureLimits{16384, 2048, 16384, 2048, 1};

[[nodiscard]] std::uint8_t max_sample_count(MTL::Device& device) noexcept {
  for (const std::uint8_t count : {std::uint8_t{8}, std::uint8_t{4}, std::uint8_t{2}}) {
    if (device.supportsTextureSampleCount(count)) {
      return count;
    }
  }
  return 1;
}

[[nodiscard]] MTL::ResourceOptions untracked_shared_options() noexcept {
  return static_cast<MTL::ResourceOptions>(MTL::ResourceStorageModeShared |
                                           MTL::ResourceCPUCacheModeDefaultCache |
                                           MTL::ResourceHazardTrackingModeUntracked);
}

// The format a texture view takes: the view's own, the texture's when the
// view names Unknown, and the stencil reading format for a stencil plane.
[[nodiscard]] MTL::PixelFormat view_pixel_format(const TextureDescription& texture,
                                                 const TextureViewDescription& view) noexcept {
  if (view.plane == TexturePlane::Stencil) {
    return texture.format == Format::D24UnormS8Uint ? MTL::PixelFormatX24_Stencil8
                                                    : MTL::PixelFormatX32_Stencil8;
  }
  const Format format = view.format == Format::Unknown ? texture.format : view.format;
  return pixel_format(format);
}

[[nodiscard]] std::uint32_t texture_slices(const TextureDescription& d) noexcept {
  switch (d.dimension) {
  case TextureDimension::TextureCube:
    return 6U * d.array_size;
  case TextureDimension::Texture3D:
    return 1U;
  case TextureDimension::Texture2D:
  case TextureDimension::Texture2DArray:
    return d.array_size;
  }
  return d.array_size;
}

[[nodiscard]] bool has_srgb_twin(Format format) noexcept {
  switch (format) {
  case Format::R8G8B8A8Unorm:
  case Format::R8G8B8A8UnormSrgb:
  case Format::B8G8R8A8Unorm:
  case Format::B8G8R8A8UnormSrgb:
  case Format::BC1Unorm:
  case Format::BC1UnormSrgb:
  case Format::BC2Unorm:
  case Format::BC2UnormSrgb:
  case Format::BC3Unorm:
  case Format::BC3UnormSrgb:
  case Format::BC7Unorm:
  case Format::BC7UnormSrgb:
    return true;
  default:
    return false;
  }
}

// The Metal texture a view's descriptor names: the texture itself when the
// view covers it whole in its own format and dimension (texture_view stays
// null), else a new texture view (owned by the caller), or null when Metal
// refuses one.
struct ViewTexture {
  MTL::Texture* texture{};
  MTL::Texture* owned_view{};
};
[[nodiscard]] ViewTexture make_view_texture(MTL::Texture& texture, const TextureDescription& d,
                                            const TextureViewDescription& view) {
  const std::uint32_t mips = view.mip_count == 0U
                                 ? static_cast<std::uint32_t>(d.mip_levels - view.first_mip)
                                 : view.mip_count;
  const std::uint32_t total_slices = texture_slices(d);
  const std::uint32_t slices =
      view.slice_count == 0U ? total_slices - view.first_slice : view.slice_count;
  const MTL::PixelFormat format = view_pixel_format(d, view);
  const bool whole = format == pixel_format(d.format) && view.dimension == d.dimension &&
                     view.first_mip == 0U && mips == d.mip_levels && view.first_slice == 0U &&
                     slices == total_slices;
  if (whole) {
    return ViewTexture{&texture, nullptr};
  }
  const std::uint32_t array_size =
      view.dimension == TextureDimension::TextureCube ? slices / 6U : slices;
  MTL::Texture* created = texture.newTextureView(
      format, texture_type(TextureShape{view.dimension, array_size, d.sample_count}),
      NS::Range::Make(view.first_mip, mips), NS::Range::Make(view.first_slice, slices));
  return ViewTexture{created, created};
}

} // namespace

BackendResult create_metal_backend(const BackendOptions& options) {
  return MetalBackend::create(options);
}

MetalBackend::MetalBackend(const BackendOptions& options, MTL::Device* device,
                           MTL::CommandQueue* queue, MTL::SharedEvent* frame_event,
                           const Capabilities& capabilities, FormatSupport format_support)
    : options_(options), device_(device), queue_(queue), frame_event_(frame_event),
      capabilities_(capabilities), format_support_(format_support) {}

// The context ends any open encoder and drops an uncommitted command buffer;
// the device destroyed every resource before this (device.hpp, destructor),
// so the heaps are empty when the memory releases them.
MetalBackend::~MetalBackend() {
  const AutoreleaseScope pool;
  context_.reset();
  descriptors_.reset();
  writable_.reset();
  memory_.reset();
  frame_event_->release();
  queue_->release();
  device_->release();
}

// Takes the system default device (the game runs on it), creates the command
// queue and the frame event, reads the feature flags from the device and
// creates the two descriptor heaps as buffers of IRDescriptorTableEntry. The
// required capabilities are checked by name (backend.hpp) and the
// converter's bind points against the ones bound here.
BackendResult MetalBackend::create(const BackendOptions& options) {
  const AutoreleaseScope pool;
  const std::string_view mismatch = irconverter_bind_point_mismatch();
  if (!mismatch.empty()) {
    return BackendResult::failure(DeviceError::BackendInitializationFailed, mismatch);
  }
  if (options.binding_structure == nullptr ||
      options.binding_structure->argument_buffer_bytes != kEngineArgumentBufferBytes) {
    return BackendResult::failure(DeviceError::BackendInitializationFailed,
                                  "the binding structure is not the converter's 160-byte "
                                  "top-level argument buffer");
  }
  MTL::Device* device = MTL::CreateSystemDefaultDevice();
  if (device == nullptr) {
    return BackendResult::failure(DeviceError::NoAdapter, "MTLCreateSystemDefaultDevice");
  }
  if (device->argumentBuffersSupport() < MTL::ArgumentBuffersTier2) {
    device->release();
    return BackendResult::failure(DeviceError::RequiredCapabilityMissing,
                                  name(RequiredCapability::TypedDescriptorArrays));
  }
  // A device below GPUFamilyApple7 is refused. Every Apple7 device supports the
  // Metal 3 family the architecture names (docs/architecture/shaders.md,
  // "Targets"), so this one check is the shader floor.
  if (!device->supportsFamily(MTL::GPUFamilyApple7)) {
    device->release();
    return BackendResult::failure(DeviceError::RequiredCapabilityMissing,
                                  name(RequiredCapability::ShaderFloor));
  }
  MTL::CommandQueue* queue = device->newCommandQueue();
  if (queue == nullptr) {
    device->release();
    return BackendResult::failure(DeviceError::BackendInitializationFailed,
                                  "newCommandQueue returned no command queue");
  }
  MTL::SharedEvent* frame_event = device->newSharedEvent();
  if (frame_event == nullptr) {
    queue->release();
    device->release();
    return BackendResult::failure(DeviceError::BackendInitializationFailed,
                                  "newSharedEvent returned no frame event");
  }

  Capabilities capabilities{};
  capabilities.backend = BackendKind::Metal;
  const std::string device_name = text_of(device->name());
  const std::size_t name_length = std::min(device_name.size(), kDeviceNameCapacity);
  std::memcpy(capabilities.device_name.data(), device_name.data(), name_length);
  capabilities.device_name_length = static_cast<std::uint8_t>(name_length);
  capabilities.typed_descriptor_arrays = true;
  capabilities.shader_floor = true;
  // Reported absent until a backend proves them (backend.hpp). Timestamp
  // queries are off in this slice: the game needs no GPU timestamps for
  // correctness, and Metal's counter sample buffers are a later slice;
  // AbsoluteTime pools are KindUnsupported (queries.hpp).
  capabilities.async_compute = false;
  capabilities.mesh_shaders = false;
  capabilities.ray_tracing = false;
  capabilities.timestamp_queries = false;
  capabilities.texture_limits = kApple7TextureLimits;
  capabilities.texture_limits.max_sample_count = max_sample_count(*device);
  // Every Apple-family GPU keeps a render pass's attachments in tile memory,
  // and the floor above is GPUFamilyApple7, so memoryless render and depth
  // targets are always available.
  capabilities.texture_limits.memoryless = true;
  capabilities.uniform_buffer_alignment = kBufferOffsetAlignment;
  capabilities.surface_kind = platform::NativeSurfaceKind::MetalLayer;
  capabilities.frames_in_flight = options.frames_in_flight;
  capabilities.descriptors = options.descriptors;
  capabilities.transient_bytes_per_frame = options.transient_bytes_per_frame;
  const FormatSupport support{device->isDepth24Stencil8PixelFormatSupported(),
                              device->supportsBCTextureCompression()};

  // From here the backend owns device, queue and event; an early return
  // destroys it, which releases them (E.4: no object survives a failure).
  std::unique_ptr<MetalBackend> backend(
      new MetalBackend(options, device, queue, frame_event, capabilities, support));
  set_label(*queue, "bw.rhi.metal.queue");
  set_label(*frame_event, "bw.rhi.metal.frame_event");

  const std::uint64_t entry_bytes = sizeof(IRDescriptorTableEntry);
  const std::uint64_t resource_entries =
      static_cast<std::uint64_t>(options.descriptors.read) + options.descriptors.storage;
  MTL::Buffer* resource_heap =
      device->newBuffer(resource_entries * entry_bytes, untracked_shared_options());
  MTL::Buffer* sampler_heap =
      device->newBuffer(static_cast<std::uint64_t>(options.descriptors.sampler) * entry_bytes,
                        untracked_shared_options());
  const std::string_view failed_step = backend->initialize(resource_heap, sampler_heap);
  if (!failed_step.empty()) {
    return BackendResult::failure(DeviceError::BackendInitializationFailed, failed_step);
  }
  return BackendResult::success(std::move(backend));
}

std::string_view MetalBackend::initialize(MTL::Buffer* resource_heap, MTL::Buffer* sampler_heap) {
  if (resource_heap == nullptr || sampler_heap == nullptr) {
    if (resource_heap != nullptr) {
      resource_heap->release();
    }
    if (sampler_heap != nullptr) {
      sampler_heap->release();
    }
    return "newBuffer returned no descriptor heap buffer";
  }
  std::memset(resource_heap->contents(), 0, resource_heap->length());
  std::memset(sampler_heap->contents(), 0, sampler_heap->length());
  set_label(*resource_heap, "bw.rhi.metal.resource_descriptor_heap");
  set_label(*sampler_heap, "bw.rhi.metal.sampler_descriptor_heap");
  descriptors_ = std::make_unique<DescriptorHeaps>(
      DescriptorHeapBuffers{resource_heap, sampler_heap}, options_.descriptors);
  memory_ = std::make_unique<ResourceMemory>(*device_, recording_frame_);
  writable_ = std::make_unique<WritableResources>();

  // One untracked ring per frame in flight, of the device's transient budget,
  // that never grows (invariant 7).
  std::vector<MTL::Buffer*> rings;
  for (std::uint32_t i = 0; i < options_.frames_in_flight; ++i) {
    MTL::Buffer* ring =
        device_->newBuffer(options_.transient_bytes_per_frame, untracked_shared_options());
    if (ring == nullptr) {
      for (MTL::Buffer* created : rings) {
        created->release();
      }
      return "newBuffer returned no transient ring buffer";
    }
    set_label(*ring, "bw.rhi.metal.transient_ring");
    rings.push_back(ring);
  }
  context_ = std::make_unique<MetalContext>(*this, rings);
  return {};
}

std::string_view MetalBackend::fail_with(std::string text) {
  error_text_ = std::move(text);
  return error_text_;
}

FormatCapability MetalBackend::format_capabilities(Format format) const noexcept {
  return format_capability(format, format_support_);
}

// ---- Uploads ----------------------------------------------------------------

// resources.hpp, Initial data: the bytes are in the resource when creation
// returns. A private resource is filled by a blit from a temporary shared
// buffer on its own command buffer, waited for here (a creation-time path,
// blocking by design, and it never spins: waitUntilCompleted blocks).
// The copy is committed and waited for at creation, so the resource is
// complete when creation returns, which is this RHI's contract.
std::string_view MetalBackend::finish_upload(MTL::CommandBuffer& commands) {
  commands.commit();
  commands.waitUntilCompleted();
  if (commands.status() != MTL::CommandBufferStatusCompleted) {
    return fail_with("initial data upload: " + error_text(commands.error(), "the copy failed"));
  }
  return {};
}

std::string_view MetalBackend::upload_buffer(MTL::Buffer& destination,
                                             std::span<const std::byte> bytes) {
  const ScopedRelease<MTL::Buffer> staging{
      device_->newBuffer(bytes.data(), bytes.size(), untracked_shared_options())};
  if (staging.get() == nullptr) {
    return fail_with("initial data upload: newBuffer returned no staging buffer");
  }
  MTL::CommandBuffer* commands = queue_->commandBuffer();
  MTL::BlitCommandEncoder* blit = commands->blitCommandEncoder();
  blit->copyFromBuffer(staging.get(), 0, &destination, 0, bytes.size());
  blit->endEncoding();
  return finish_upload(*commands);
}

std::string_view MetalBackend::upload_texture(MTL::Texture& destination,
                                              const TextureDescription& description,
                                              std::span<const SubresourceData> data) {
  // Each subresource in subresource_index order, at a 256-byte aligned offset
  // of one staging buffer, with the caller's pitches.
  struct Placed {
    std::uint64_t offset{};
    std::uint64_t bytes{};
  };
  std::vector<Placed> placed(data.size());
  std::uint64_t total = 0;
  for (std::size_t i = 0; i < data.size(); ++i) {
    const std::uint32_t mip = static_cast<std::uint32_t>(i % description.mip_levels);
    const std::uint32_t width = std::max(description.extent.width >> mip, 1U);
    const std::uint32_t height = std::max(description.extent.height >> mip, 1U);
    const std::uint32_t depth = description.dimension == TextureDimension::Texture3D
                                    ? std::max(description.extent.depth >> mip, 1U)
                                    : 1U;
    const FormatInfo info = format_info(description.format);
    const std::uint32_t rows = (height + info.block_height - 1U) / info.block_height;
    const std::uint64_t bytes = static_cast<std::uint64_t>(depth - 1U) * data[i].slice_pitch +
                                static_cast<std::uint64_t>(rows - 1U) * data[i].row_pitch +
                                row_bytes(description.format, width);
    total = bw::memory::align_up(total, std::uint64_t{kBufferOffsetAlignment});
    placed[i] = Placed{total, bytes};
    total += bytes;
  }
  const ScopedRelease<MTL::Buffer> staging{device_->newBuffer(total, untracked_shared_options())};
  if (staging.get() == nullptr) {
    return fail_with("initial data upload: newBuffer returned no staging buffer");
  }
  auto* base = static_cast<std::byte*>(staging->contents());
  for (std::size_t i = 0; i < data.size(); ++i) {
    std::memcpy(base + placed[i].offset, data[i].bytes.data(), placed[i].bytes);
  }
  MTL::CommandBuffer* commands = queue_->commandBuffer();
  MTL::BlitCommandEncoder* blit = commands->blitCommandEncoder();
  for (std::size_t i = 0; i < data.size(); ++i) {
    const std::uint32_t mip = static_cast<std::uint32_t>(i % description.mip_levels);
    const std::uint32_t slice = static_cast<std::uint32_t>(i / description.mip_levels);
    const bool volume = description.dimension == TextureDimension::Texture3D;
    const MTL::Size size =
        MTL::Size::Make(std::max(description.extent.width >> mip, 1U),
                        std::max(description.extent.height >> mip, 1U),
                        volume ? std::max(description.extent.depth >> mip, 1U) : 1U);
    blit->copyFromBuffer(staging.get(), placed[i].offset, data[i].row_pitch,
                         volume ? data[i].slice_pitch : 0U, size, &destination, slice, mip,
                         MTL::Origin::Make(0, 0, 0));
  }
  blit->endEncoding();
  return finish_upload(*commands);
}

// ---- Resources ---------------------------------------------------------------

// Takes heapBufferSizeAndAlign, raises the alignment to the required one,
// finds or allocates a block and creates the buffer at its offset with
// newBuffer(size, options, offset). The required alignment is
// kBufferOffsetAlignment, or kBufferBackedTextureOffsetAlignment when the
// buffer has the ShaderResource or UnorderedAccess usage. Storage: an Upload
// buffer is shared and persistently mapped (resources.hpp, Locks); every
// other buffer is private.
BufferResult MetalBackend::create_buffer(const BufferDescription& description,
                                         std::span<const std::byte> initial_data) {
  const AutoreleaseScope pool;
  const bool upload = has_usage(description.usage, BufferUsage::Upload);
  const StorageClass storage = upload ? StorageClass::Shared : StorageClass::Private;
  const MTL::ResourceOptions options = resource_options(storage);
  MTL::SizeAndAlign size = device_->heapBufferSizeAndAlign(description.size, options);
  const bool shader_visible = has_usage(description.usage, BufferUsage::ShaderResource) ||
                              has_usage(description.usage, BufferUsage::UnorderedAccess);
  size.align = std::max<NS::UInteger>(
      size.align, shader_visible ? kBufferBackedTextureOffsetAlignment : kBufferOffsetAlignment);
  const std::optional<HeapPlacement> placement = memory_->allocate(storage, size);
  if (!placement.has_value()) {
    return BufferResult::failure(ResourceError::OutOfMemory, description.debug_name);
  }
  MTL::Buffer* buffer =
      placement->block->heap()->newBuffer(description.size, options, placement->range.offset);
  if (buffer == nullptr) {
    memory_->free(*placement);
    return BufferResult::failure(ResourceError::BackendRejected,
                                 fail_with("newBuffer at a heap offset returned no buffer"));
  }
  set_label(*buffer, description.debug_name);
  if (!initial_data.empty()) {
    std::string_view upload_error{};
    if (upload) {
      std::memcpy(buffer->contents(), initial_data.data(), initial_data.size());
    } else {
      upload_error = upload_buffer(*buffer, initial_data);
    }
    if (!upload_error.empty()) {
      buffer->release();
      memory_->free(*placement);
      return BufferResult::failure(ResourceError::BackendRejected, upload_error);
    }
  }
  return BufferResult::success(Ref<Buffer>(
      new MetalBuffer(*options_.delete_queue, description, buffer, *placement, *memory_)));
}

// Builds the descriptor from the RHI's, takes heapTextureSizeAndAlign, finds
// or allocates a private block and creates the texture with
// newTexture(descriptor, offset); a memoryless texture is created with
// newTexture(descriptor) and no block.
TextureResult MetalBackend::create_texture(const TextureDescription& description,
                                           std::span<const SubresourceData> initial_data) {
  const AutoreleaseScope pool;
  const ScopedRelease<MTL::TextureDescriptor> descriptor{MTL::TextureDescriptor::alloc()->init()};
  descriptor->setTextureType(texture_type(
      TextureShape{description.dimension, description.array_size, description.sample_count}));
  descriptor->setPixelFormat(pixel_format(description.format));
  descriptor->setWidth(description.extent.width);
  descriptor->setHeight(description.extent.height);
  descriptor->setDepth(
      description.dimension == TextureDimension::Texture3D ? description.extent.depth : 1U);
  descriptor->setMipmapLevelCount(description.mip_levels);
  descriptor->setSampleCount(description.sample_count);
  descriptor->setArrayLength(
      description.dimension == TextureDimension::Texture3D ? 1U : description.array_size);
  NS::UInteger usage = MTL::TextureUsageUnknown;
  if (has_usage(description.usage, TextureUsage::ShaderResource)) {
    usage |= MTL::TextureUsageShaderRead;
  }
  if (has_usage(description.usage, TextureUsage::RenderTarget) ||
      has_usage(description.usage, TextureUsage::DepthStencil)) {
    usage |= MTL::TextureUsageRenderTarget;
  }
  if (has_usage(description.usage, TextureUsage::UnorderedAccess)) {
    usage |= MTL::TextureUsageShaderWrite;
  }
  // A view may name the sRGB twin of the format or the stencil plane
  // (resources.hpp, validate_texture_view); Metal requires the
  // pixel-format-view usage for either.
  if (has_srgb_twin(description.format) || format_info(description.format).has_stencil) {
    usage |= MTL::TextureUsagePixelFormatView;
  }
  descriptor->setUsage(static_cast<MTL::TextureUsage>(usage));
  descriptor->setCpuCacheMode(MTL::CPUCacheModeDefaultCache);
  descriptor->setHazardTrackingMode(MTL::HazardTrackingModeTracked);

  // A memoryless texture lives in tile memory for one render pass and takes
  // no heap memory, so it is created from the device, not at a heap offset.
  // The device rejects initial data for it (validate_initial_data).
  if (has_usage(description.usage, TextureUsage::Memoryless)) {
    descriptor->setStorageMode(MTL::StorageModeMemoryless);
    MTL::Texture* memoryless = device_->newTexture(descriptor.get());
    if (memoryless == nullptr) {
      return TextureResult::failure(ResourceError::BackendRejected,
                                    fail_with("newTexture returned no memoryless texture"));
    }
    set_label(*memoryless, description.debug_name);
    return TextureResult::success(
        Ref<Texture>(new MetalTexture(*options_.delete_queue, description, memoryless)));
  }
  descriptor->setStorageMode(MTL::StorageModePrivate);

  const MTL::SizeAndAlign size = device_->heapTextureSizeAndAlign(descriptor.get());
  const std::optional<HeapPlacement> placement = memory_->allocate(StorageClass::Private, size);
  if (!placement.has_value()) {
    return TextureResult::failure(ResourceError::OutOfMemory, description.debug_name);
  }
  MTL::Texture* texture =
      placement->block->heap()->newTexture(descriptor.get(), placement->range.offset);
  if (texture == nullptr) {
    memory_->free(*placement);
    return TextureResult::failure(ResourceError::BackendRejected,
                                  fail_with("newTexture at a heap offset returned no texture"));
  }
  set_label(*texture, description.debug_name);
  if (!initial_data.empty()) {
    const std::string_view upload_error = upload_texture(*texture, description, initial_data);
    if (!upload_error.empty()) {
      texture->release();
      memory_->free(*placement);
      return TextureResult::failure(ResourceError::BackendRejected, upload_error);
    }
  }
  return TextureResult::success(Ref<Texture>(
      new MetalTexture(*options_.delete_queue, description, texture, *placement, *memory_)));
}

// ---- Views and samplers ----------------------------------------------------------

// A texture descriptor is IRDescriptorTableSetTexture(texture, 0, 0), a
// buffer descriptor IRDescriptorTableSetBuffer(address, length). The entry is
// written at creation, at the slot the view was given, in the heap of the
// slot's pool (metal_memory.hpp). The entry is not cleared when the view is
// destroyed: the slot returns to the pool and no shader may read a free slot.
ShaderResourceViewResult
MetalBackend::create_shader_resource_view(Ref<Buffer> buffer, const BufferViewDescription& view,
                                          DescriptorSlot slot) {
  const auto& metal_buffer = static_cast<const MetalBuffer&>(*buffer);
  const std::uint64_t size = view.size == 0U ? buffer->description().size - view.offset : view.size;
  IRDescriptorTableSetBuffer(&descriptors_->entry(slot),
                             metal_buffer.buffer()->gpuAddress() + view.offset, size);
  return ShaderResourceViewResult::success(Ref<ShaderResourceView>(new MetalShaderResourceView(
      *options_.delete_queue, Ref<Viewable>(buffer), slot, *options_.slot_pools, nullptr)));
}

ShaderResourceViewResult
MetalBackend::create_shader_resource_view(Ref<Texture> texture, const TextureViewDescription& view,
                                          DescriptorSlot slot) {
  const AutoreleaseScope pool;
  auto& metal_texture = static_cast<MetalTexture&>(*texture);
  const ViewTexture target =
      make_view_texture(*metal_texture.texture(), texture->description(), view);
  if (target.texture == nullptr) {
    return ShaderResourceViewResult::failure(ResourceError::BackendRejected,
                                             fail_with("newTextureView returned no texture"));
  }
  IRDescriptorTableSetTexture(&descriptors_->entry(slot), target.texture, 0.0F, 0U);
  return ShaderResourceViewResult::success(Ref<ShaderResourceView>(
      new MetalShaderResourceView(*options_.delete_queue, Ref<Viewable>(texture), slot,
                                  *options_.slot_pools, target.owned_view)));
}

UnorderedAccessViewResult
MetalBackend::create_unordered_access_view(Ref<Buffer> buffer, const BufferViewDescription& view,
                                           DescriptorSlot slot) {
  const auto& metal_buffer = static_cast<const MetalBuffer&>(*buffer);
  const std::uint64_t size = view.size == 0U ? buffer->description().size - view.offset : view.size;
  IRDescriptorTableSetBuffer(&descriptors_->entry(slot),
                             metal_buffer.buffer()->gpuAddress() + view.offset, size);
  writable_->add(*metal_buffer.buffer());
  return UnorderedAccessViewResult::success(Ref<UnorderedAccessView>(new MetalUnorderedAccessView(
      *options_.delete_queue, Ref<Viewable>(buffer), slot, *options_.slot_pools, nullptr,
      *metal_buffer.buffer(), *writable_)));
}

UnorderedAccessViewResult
MetalBackend::create_unordered_access_view(Ref<Texture> texture, const TextureViewDescription& view,
                                           DescriptorSlot slot) {
  const AutoreleaseScope pool;
  auto& metal_texture = static_cast<MetalTexture&>(*texture);
  const ViewTexture target =
      make_view_texture(*metal_texture.texture(), texture->description(), view);
  if (target.texture == nullptr) {
    return UnorderedAccessViewResult::failure(ResourceError::BackendRejected,
                                              fail_with("newTextureView returned no texture"));
  }
  IRDescriptorTableSetTexture(&descriptors_->entry(slot), target.texture, 0.0F, 0U);
  writable_->add(*target.texture);
  return UnorderedAccessViewResult::success(Ref<UnorderedAccessView>(new MetalUnorderedAccessView(
      *options_.delete_queue, Ref<Viewable>(texture), slot, *options_.slot_pools, target.owned_view,
      *target.texture, *writable_)));
}

// Maps the filter to min, mag and mip filters (both anisotropic filters
// linear), sets the anisotropy only for the anisotropic filters, the address
// modes, LOD clamps, compare function and border color, with
// setSupportArgumentBuffers for the converter. The descriptor's LOD bias is
// the description's mip_bias (the converter applies it in the shader from
// the entry's metadata, IRDescriptorTableSetSampler). Sharing equal samplers
// is the device's (device.hpp), so no cache here.
SamplerResult MetalBackend::create_sampler(const SamplerDescription& description,
                                           DescriptorSlot slot) {
  const AutoreleaseScope pool;
  const ScopedRelease<MTL::SamplerDescriptor> descriptor{MTL::SamplerDescriptor::alloc()->init()};
  bool anisotropic = false;
  switch (description.filter) {
  case Filter::Point:
    descriptor->setMinFilter(MTL::SamplerMinMagFilterNearest);
    descriptor->setMagFilter(MTL::SamplerMinMagFilterNearest);
    descriptor->setMipFilter(MTL::SamplerMipFilterNearest);
    break;
  case Filter::Bilinear:
    descriptor->setMinFilter(MTL::SamplerMinMagFilterLinear);
    descriptor->setMagFilter(MTL::SamplerMinMagFilterLinear);
    descriptor->setMipFilter(MTL::SamplerMipFilterNearest);
    break;
  case Filter::Trilinear:
    descriptor->setMinFilter(MTL::SamplerMinMagFilterLinear);
    descriptor->setMagFilter(MTL::SamplerMinMagFilterLinear);
    descriptor->setMipFilter(MTL::SamplerMipFilterLinear);
    break;
  case Filter::AnisotropicPoint:
  case Filter::AnisotropicLinear:
    descriptor->setMinFilter(MTL::SamplerMinMagFilterLinear);
    descriptor->setMagFilter(MTL::SamplerMinMagFilterLinear);
    descriptor->setMipFilter(MTL::SamplerMipFilterLinear);
    anisotropic = true;
    break;
  }
  const NS::UInteger anisotropy =
      anisotropic ? std::clamp<NS::UInteger>(description.max_anisotropy, 1U, 16U) : 1U;
  descriptor->setMaxAnisotropy(anisotropy);
  descriptor->setSAddressMode(address_mode(description.address_u));
  descriptor->setTAddressMode(address_mode(description.address_v));
  descriptor->setRAddressMode(address_mode(description.address_w));
  descriptor->setLodMinClamp(description.min_mip);
  descriptor->setLodMaxClamp(description.max_mip);
  descriptor->setCompareFunction(compare_function(description.compare));
  descriptor->setBorderColor(border_color(description.border_color));
  descriptor->setSupportArgumentBuffers(true);
  MTL::SamplerState* state = device_->newSamplerState(descriptor.get());
  if (state == nullptr) {
    return SamplerResult::failure(ResourceError::BackendRejected,
                                  fail_with("newSamplerState returned no sampler"));
  }
  IRDescriptorTableSetSampler(&descriptors_->entry(slot), state, description.mip_bias);
  return SamplerResult::success(
      Ref<Sampler>(new MetalSampler(*options_.delete_queue, description, slot, state)));
}

// ---- State objects -------------------------------------------------------------

RasterizerStateResult
MetalBackend::create_rasterizer_state(const RasterizerDescription& description) {
  return RasterizerStateResult::success(
      Ref<RasterizerState>(new MetalRasterizerState(*options_.delete_queue, description)));
}

// Builds an MTL::DepthStencilDescriptor with the depth compare and write, a
// front stencil descriptor when front stencil is on, a back one when back
// stencil is on or the front one copied in single-face mode, each with the
// read and write masks.
DepthStencilStateResult
MetalBackend::create_depth_stencil_state(const DepthStencilDescription& description) {
  const AutoreleaseScope pool;
  const ScopedRelease<MTL::DepthStencilDescriptor> descriptor{
      MTL::DepthStencilDescriptor::alloc()->init()};
  descriptor->setDepthCompareFunction(compare_function(description.depth_test));
  descriptor->setDepthWriteEnabled(description.depth_write);
  const auto make_face = [&](const StencilFace& face) {
    MTL::StencilDescriptor* stencil = MTL::StencilDescriptor::alloc()->init();
    stencil->setStencilCompareFunction(compare_function(face.test));
    stencil->setStencilFailureOperation(stencil_operation(face.fail));
    stencil->setDepthFailureOperation(stencil_operation(face.depth_fail));
    stencil->setDepthStencilPassOperation(stencil_operation(face.pass));
    stencil->setReadMask(description.stencil_read_mask);
    stencil->setWriteMask(description.stencil_write_mask);
    return stencil;
  };
  if (description.front.enable) {
    const ScopedRelease<MTL::StencilDescriptor> front{make_face(description.front)};
    descriptor->setFrontFaceStencil(front.get());
  }
  if (description.back.enable) {
    const ScopedRelease<MTL::StencilDescriptor> back{make_face(description.back)};
    descriptor->setBackFaceStencil(back.get());
  } else if (description.front.enable) {
    const ScopedRelease<MTL::StencilDescriptor> back{make_face(description.front)};
    descriptor->setBackFaceStencil(back.get());
  }
  MTL::DepthStencilState* state = device_->newDepthStencilState(descriptor.get());
  if (state == nullptr) {
    return DepthStencilStateResult::failure(ResourceError::BackendRejected,
                                            fail_with("newDepthStencilState returned no state"));
  }
  return DepthStencilStateResult::success(Ref<DepthStencilState>(
      new MetalDepthStencilState(*options_.delete_queue, description, state)));
}

BlendStateResult MetalBackend::create_blend_state(const BlendDescription& description) {
  return BlendStateResult::success(
      Ref<BlendState>(new MetalBlendState(*options_.delete_queue, description)));
}

// ---- Pipelines -------------------------------------------------------------------

// The converter's library is created from the code bytes with
// dispatch_data_create and newLibrary(dispatch_data, &error); the function is
// taken from the library by the artifact's entry point (the shader system
// names the function after it).
MetalBackend::LoadedFunction MetalBackend::load_function(const shaders::Artifact& artifact,
                                                         PipelineError& error,
                                                         std::string_view& detail) {
  dispatch_data_t data = dispatch_data_create(artifact.code.data(), artifact.code.size(), nullptr,
                                              DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  NS::Error* library_error = nullptr;
  MTL::Library* library = device_->newLibrary(data, &library_error);
  dispatch_release(data);
  std::string shader_name{artifact.header.shader_name};
  if (library == nullptr) {
    error = PipelineError::ShaderLibraryRejected;
    detail = fail_with(shader_name + ": " + error_text(library_error, "newLibrary returned none"));
    return {};
  }
  MTL::Function* function = library->newFunction(make_string(artifact.header.entry_point));
  if (function == nullptr) {
    library->release();
    error = PipelineError::EntryPointNotFound;
    detail = fail_with(shader_name + ": the library has no function named '" +
                       std::string{artifact.header.entry_point} + "'");
    return {};
  }
  return LoadedFunction{library, function};
}

// Builds a RenderPipelineDescriptor with the vertex and fragment functions,
// the color attachments' formats and blend states, the depth and stencil
// formats and the sample count, and no vertex descriptor (bindless), and
// calls newRenderPipelineState(descriptor, &error). A non-empty vertex layout
// is VertexLayoutUnsupported in this slice (pipeline_states.hpp): a stage-in
// function would have to be synthesized with the converter, which is not
// shipped. WrongTarget is not checked here: the device checks the artifacts'
// target against this backend's before calling (device.cpp,
// validate_graphics).
GraphicsPipelineResult
MetalBackend::create_graphics_pipeline(const GraphicsPipelineDescription& description,
                                       const PipelineBinding& binding) {
  const AutoreleaseScope pool;
  if (!description.vertex_layout.empty()) {
    return GraphicsPipelineResult::failure(PipelineError::VertexLayoutUnsupported,
                                           description.debug_name);
  }
  PipelineError error = PipelineError::None;
  std::string_view detail{};
  const LoadedFunction vertex = load_function(*description.vertex_shader, error, detail);
  if (vertex.function == nullptr) {
    return GraphicsPipelineResult::failure(error, detail);
  }
  LoadedFunction pixel{};
  if (description.pixel_shader != nullptr) {
    pixel = load_function(*description.pixel_shader, error, detail);
    if (pixel.function == nullptr) {
      vertex.function->release();
      vertex.library->release();
      return GraphicsPipelineResult::failure(error, detail);
    }
  }
  const ScopedRelease<MTL::RenderPipelineDescriptor> descriptor{
      MTL::RenderPipelineDescriptor::alloc()->init()};
  descriptor->setVertexFunction(vertex.function);
  descriptor->setFragmentFunction(pixel.function);
  const BlendDescription& blend = description.blend->description();
  for (std::uint32_t i = 0; i < description.targets.color_count; ++i) {
    MTL::RenderPipelineColorAttachmentDescriptor* attachment =
        descriptor->colorAttachments()->object(i);
    const RenderTargetBlend& target = blend.independent ? blend.targets[i] : blend.targets[0];
    attachment->setPixelFormat(pixel_format(description.targets.color_formats[i]));
    attachment->setBlendingEnabled(blending_enabled(target));
    attachment->setSourceRGBBlendFactor(blend_factor(target.color_source));
    attachment->setDestinationRGBBlendFactor(blend_factor(target.color_destination));
    attachment->setRgbBlendOperation(blend_operation(target.color_op));
    attachment->setSourceAlphaBlendFactor(blend_factor(target.alpha_source));
    attachment->setDestinationAlphaBlendFactor(blend_factor(target.alpha_destination));
    attachment->setAlphaBlendOperation(blend_operation(target.alpha_op));
    attachment->setWriteMask(color_write_mask(target.write_mask));
  }
  const Format depth_format = description.targets.depth_stencil_format;
  if (depth_format != Format::Unknown) {
    descriptor->setDepthAttachmentPixelFormat(pixel_format(depth_format));
    if (format_info(depth_format).has_stencil) {
      descriptor->setStencilAttachmentPixelFormat(pixel_format(depth_format));
    }
  }
  descriptor->setRasterSampleCount(description.targets.sample_count);
  descriptor->setAlphaToCoverageEnabled(blend.alpha_to_coverage);
  set_label(*descriptor.get(), description.debug_name);
  NS::Error* pipeline_error = nullptr;
  MTL::RenderPipelineState* state =
      device_->newRenderPipelineState(descriptor.get(), &pipeline_error);
  vertex.function->release();
  vertex.library->release();
  if (pixel.function != nullptr) {
    pixel.function->release();
    pixel.library->release();
  }
  if (state == nullptr) {
    return GraphicsPipelineResult::failure(
        PipelineError::BackendRejected,
        fail_with(std::string{description.debug_name} + ": " +
                  error_text(pipeline_error, "newRenderPipelineState returned none")));
  }
  return GraphicsPipelineResult::success(Ref<GraphicsPipeline>(
      new MetalGraphicsPipeline(*options_.delete_queue, description, binding, state)));
}

// Created from the function as for graphics:
// newComputePipelineState(function, &error).
ComputePipelineResult
MetalBackend::create_compute_pipeline(const ComputePipelineDescription& description,
                                      const PipelineBinding& binding) {
  const AutoreleaseScope pool;
  PipelineError error = PipelineError::None;
  std::string_view detail{};
  const LoadedFunction compute = load_function(*description.compute_shader, error, detail);
  if (compute.function == nullptr) {
    return ComputePipelineResult::failure(error, detail);
  }
  NS::Error* pipeline_error = nullptr;
  MTL::ComputePipelineState* state =
      device_->newComputePipelineState(compute.function, &pipeline_error);
  compute.function->release();
  compute.library->release();
  if (state == nullptr) {
    return ComputePipelineResult::failure(
        PipelineError::BackendRejected,
        fail_with(std::string{description.debug_name} + ": " +
                  error_text(pipeline_error, "newComputePipelineState returned none")));
  }
  return ComputePipelineResult::success(Ref<ComputePipeline>(
      new MetalComputePipeline(*options_.delete_queue, description, binding, state)));
}

// ---- Fences, staging and queries -------------------------------------------------

// The GPU fence creates a shared event of its own.
GpuFenceResult MetalBackend::create_fence(std::string_view debug_name) {
  const AutoreleaseScope pool;
  MTL::SharedEvent* event = device_->newSharedEvent();
  if (event == nullptr) {
    return GpuFenceResult::failure(ResourceError::BackendRejected,
                                   fail_with("newSharedEvent returned no event"));
  }
  set_label(*event, debug_name);
  return GpuFenceResult::success(
      Ref<GpuFence>(new MetalGpuFence(*options_.delete_queue, debug_name, event)));
}

// The staging buffer's shadow buffer is a shared buffer from the shared
// heaps, allocated at creation with the staging size.
StagingBufferResult MetalBackend::create_staging_buffer(std::uint64_t size,
                                                        std::string_view debug_name) {
  if (size == 0U) {
    return StagingBufferResult::failure(ResourceError::ZeroSize, debug_name);
  }
  const AutoreleaseScope pool;
  const MTL::ResourceOptions options = resource_options(StorageClass::Shared);
  MTL::SizeAndAlign heap_size = device_->heapBufferSizeAndAlign(size, options);
  heap_size.align = std::max<NS::UInteger>(heap_size.align, kBufferOffsetAlignment);
  const std::optional<HeapPlacement> placement = memory_->allocate(StorageClass::Shared, heap_size);
  if (!placement.has_value()) {
    return StagingBufferResult::failure(ResourceError::OutOfMemory, debug_name);
  }
  MTL::Buffer* buffer = placement->block->heap()->newBuffer(size, options, placement->range.offset);
  if (buffer == nullptr) {
    memory_->free(*placement);
    return StagingBufferResult::failure(ResourceError::BackendRejected,
                                        fail_with("newBuffer at a heap offset returned no buffer"));
  }
  set_label(*buffer, debug_name);
  return StagingBufferResult::success(Ref<StagingBuffer>(new MetalStagingBuffer(
      *options_.delete_queue, size, debug_name, buffer, *placement, *memory_)));
}

// The occlusion results live in a buffer from the resource heap: one shared
// buffer of count 8-byte results per pool, zeroed at creation.
// AbsoluteTime pools are refused by the device (timestamp_queries is off,
// queries.hpp); the check is repeated here so the backend never creates one.
QueryPoolResult MetalBackend::create_query_pool(const QueryPoolDescription& description) {
  if (description.kind != QueryKind::Occlusion) {
    return QueryPoolResult::failure(QueryError::KindUnsupported, description.debug_name);
  }
  const AutoreleaseScope pool;
  const std::uint64_t bytes = static_cast<std::uint64_t>(description.count) * sizeof(std::uint64_t);
  const MTL::ResourceOptions options = resource_options(StorageClass::Shared);
  MTL::SizeAndAlign size = device_->heapBufferSizeAndAlign(bytes, options);
  size.align = std::max<NS::UInteger>(size.align, kBufferOffsetAlignment);
  const std::optional<HeapPlacement> placement = memory_->allocate(StorageClass::Shared, size);
  if (!placement.has_value()) {
    return QueryPoolResult::failure(QueryError::OutOfMemory, description.debug_name);
  }
  MTL::Buffer* results =
      placement->block->heap()->newBuffer(bytes, options, placement->range.offset);
  if (results == nullptr) {
    memory_->free(*placement);
    return QueryPoolResult::failure(QueryError::BackendRejected,
                                    fail_with("newBuffer at a heap offset returned no buffer"));
  }
  std::memset(results->contents(), 0, bytes);
  set_label(*results, description.debug_name);
  return QueryPoolResult::success(Ref<QueryPool>(
      new MetalQueryPool(*options_.delete_queue, description, results, *placement, *memory_)));
}

// resources.hpp, Locks: the Upload buffer is shared and persistently mapped,
// so the lock is the mapping's address plus the offset. The device checked
// the usage and the range (validate_lock); AlreadyLocked and NotLocked are
// this backend's, which holds the lock state.
LockResult MetalBackend::lock_buffer(Buffer& buffer, BufferRange range) {
  auto& metal_buffer = static_cast<MetalBuffer&>(buffer);
  if (metal_buffer.locked()) {
    return LockResult::failure(LockError::AlreadyLocked, buffer.debug_name());
  }
  metal_buffer.set_locked(true);
  auto* base = static_cast<std::byte*>(metal_buffer.buffer()->contents());
  return LockResult::success(std::span<std::byte>{base + range.offset, range.size});
}

LockError MetalBackend::unlock_buffer(Buffer& buffer) {
  auto& metal_buffer = static_cast<MetalBuffer&>(buffer);
  if (!metal_buffer.locked()) {
    return LockError::NotLocked;
  }
  metal_buffer.set_locked(false);
  return LockError::None;
}

// Lock returns the shadow buffer's contents once the fence is signaled, as
// synchronization.hpp states: Block waits on the fence's event, PollOnly
// returns NotReady while it is unsignaled; neither spins.
StagingLockResult MetalBackend::lock_staging(StagingBuffer& staging, GpuFence& fence,
                                             WaitPolicy policy) {
  auto& metal_staging = static_cast<MetalStagingBuffer&>(staging);
  const StagingError precondition = fence_wait_error(fence.state());
  if (precondition != StagingError::None) {
    return StagingLockResult::failure(precondition, fence.debug_name());
  }
  if (metal_staging.locked()) {
    return StagingLockResult::failure(StagingError::AlreadyLocked, staging.debug_name());
  }
  bool waited = false;
  if (!fence.poll()) {
    if (policy == WaitPolicy::PollOnly) {
      return StagingLockResult::failure(StagingError::NotReady, fence.debug_name());
    }
    fence.wait();
    waited = true;
  }
  metal_staging.set_locked(true);
  const auto* base = static_cast<const std::byte*>(metal_staging.buffer()->contents());
  return StagingLockResult::success(
      StagingLock{std::span<const std::byte>{base, staging.size()}, waited});
}

StagingError MetalBackend::unlock_staging(StagingBuffer& staging) {
  auto& metal_staging = static_cast<MetalStagingBuffer&>(staging);
  if (!metal_staging.locked()) {
    return StagingError::NotLocked;
  }
  metal_staging.set_locked(false);
  return StagingError::None;
}

// queries.hpp, Results: the value once the frame that ended the query has
// completed. A query not ended since its last begin is NotEnded (begin_query
// clears the ended frame, metal_context.cpp). Before completion, PollOnly is
// NotReady and Block waits for that frame, or returns NotSubmitted without
// blocking when the frame has not been submitted.
// The frame is the completion value; the value is read from the visibility
// buffer after completion.
QueryResult MetalBackend::query_result(QueryPool& pool, std::uint32_t index, WaitPolicy policy) {
  const auto& metal_pool = static_cast<const MetalQueryPool&>(pool);
  const std::uint64_t ended = metal_pool.ended_frame(index);
  if (ended == 0U) {
    return QueryResult::failure(QueryError::NotEnded, pool.debug_name());
  }
  if (completed_frame() < ended) {
    if (policy == WaitPolicy::PollOnly) {
      return QueryResult::failure(QueryError::NotReady, pool.debug_name());
    }
    if (ended > submitted_frame_.load(std::memory_order_acquire)) {
      return QueryResult::failure(QueryError::NotSubmitted, pool.debug_name());
    }
    wait_for_frame(ended);
  }
  return QueryResult::success(QueryValue{metal_pool.result(index)});
}

// ---- Viewports ---------------------------------------------------------------------

// Preconditions (backend.hpp, Viewports): the description passed
// validate_viewport and its kind is MetalLayer. Configuring a CAMetalLayer
// reports no failure, so BackendRejected is not returned.
ViewportResult MetalBackend::create_viewport(const ViewportDescription& description) {
  return ViewportResult::success(
      Ref<Viewport>(new MetalViewport(*options_.delete_queue, description, *device_)));
}

// Preconditions (backend.hpp, Viewports): the device waited idle and the
// size is non-zero; the viewport is this backend's.
Status<ViewportError> MetalBackend::resize_viewport(Viewport& viewport, Extent2 pixel_size) {
  static_cast<MetalViewport&>(viewport).resize(pixel_size);
  return Status<ViewportError>{};
}

void MetalBackend::set_vsync(Viewport& viewport, bool vsync) {
  static_cast<MetalViewport&>(viewport).apply_vsync(vsync);
}

// ---- Frames ------------------------------------------------------------------------

void MetalBackend::track_fence(MetalGpuFence& fence) { fences_written_.push_back(&fence); }

void MetalBackend::track_viewport(MetalViewport& viewport) {
  if (std::find(viewports_acquired_.begin(), viewports_acquired_.end(), &viewport) ==
      viewports_acquired_.end()) {
    viewports_acquired_.push_back(&viewport);
  }
}

// Commits the frame's work command buffer, then a separate command buffer
// that encodes encodeSignalEvent(frame event, frame) and is committed, with
// the frame number as the value and one queue. The signal rides its own
// command buffer, so a failed work buffer cannot keep the frame from
// completing. Commit happens on the submission thread that calls end_frame.
// Fault: a completed handler on both buffers reads status() and error() and
// sets the flag take_gpu_fault returns (backend.hpp). The handler holds the
// flag by shared ownership, because Metal may run it after the frame event
// has been signaled and the backend destroyed. The error text has no channel
// in the RHI and is not kept. The frame event is an MTL::SharedEvent whose
// value the CPU reads and waits on (completed_frame, wait_for_frame).
// Present (viewport.hpp, invariant 3): every drawable the frame acquired is
// presented on the frame's work command buffer, after its work: a scheduled
// handler calls present() and a completed handler releases the drawable,
// which each handler holds by the +1 take_drawable returned (the work
// buffer does not retain what it references).
// Invariant (metal_context.hpp, Back buffers): a viewport is tracked only
// after the pass that acquired it created the frame's work buffer, so
// `work` is not null whenever a drawable is taken here. A drawable taken
// with no work buffer is a programming error of this backend, not a GPU
// fault: one line naming the invariant goes to stderr and the process
// aborts rather than dereferencing a null buffer.
// No semaphore is waited here: the device bounds frames in flight with the
// frame event (lifetime.hpp, required_completion). Presenting does not show
// the window; the window shows itself (platform/window/window.hpp).
void MetalBackend::submit_frame(std::uint64_t frame) {
  const AutoreleaseScope pool;
  const std::shared_ptr<std::atomic<bool>> fault = gpu_fault_;
  const MTL::HandlerFunction record_fault = [fault](MTL::CommandBuffer* completed) {
    if (completed->status() == MTL::CommandBufferStatusError || completed->error() != nullptr) {
      fault->store(true, std::memory_order_release);
    }
  };
  MTL::CommandBuffer* work = context_->finish_frame();
  for (MetalViewport* viewport : viewports_acquired_) {
    CA::MetalDrawable* drawable = viewport->take_drawable(frame);
    if (drawable == nullptr) {
      continue;
    }
    if (work == nullptr) {
      std::fputs("bw::rhi::metal: a viewport was tracked for present in a frame with no "
                 "command buffer (metal_context.hpp, Back buffers)\n",
                 stderr);
      std::abort();
    }
    const MTL::HandlerFunction present = [drawable](MTL::CommandBuffer*) { drawable->present(); };
    const MTL::HandlerFunction release = [drawable](MTL::CommandBuffer*) { drawable->release(); };
    work->addScheduledHandler(present);
    work->addCompletedHandler(release);
  }
  viewports_acquired_.clear();
  if (work != nullptr) {
    work->addCompletedHandler(record_fault);
    work->commit();
    work->release();
  }
  MTL::CommandBuffer* signal = queue_->commandBufferWithUnretainedReferences();
  set_label(*signal, "bw.rhi.metal.frame_signal");
  signal->addCompletedHandler(record_fault);
  signal->encodeSignalEvent(frame_event_, frame);
  signal->commit();
  for (MetalGpuFence* fence : fences_written_) {
    fence->mark_submitted();
  }
  fences_written_.clear();
  submitted_frame_.store(frame, std::memory_order_release);
  recording_frame_.store(frame + 1U, std::memory_order_release);
}

std::uint64_t MetalBackend::completed_frame() const noexcept {
  return frame_event_->signaledValue();
}

// Precondition (backend.hpp): frame has been submitted; the device derives
// it from required_completion and queries check NotSubmitted first.
void MetalBackend::wait_for_frame(std::uint64_t frame) { wait_for_event(*frame_event_, frame); }

bool MetalBackend::take_gpu_fault() noexcept {
  return gpu_fault_->exchange(false, std::memory_order_acq_rel);
}

void MetalBackend::wait_idle() {
  wait_for_event(*frame_event_, submitted_frame_.load(std::memory_order_acquire));
}

// The rings of completed frames rewind and the heaps retired by then are
// released.
void MetalBackend::reset_frame_resources(std::uint64_t completed_frame) {
  context_->reset_rings(completed_frame);
  memory_->release_retired(completed_frame);
}

} // namespace bw::rhi::metal
