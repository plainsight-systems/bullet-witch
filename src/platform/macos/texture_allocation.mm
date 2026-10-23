// Native texture owner implementation; contract in texture_allocation.hpp.
// Native heap-size estimate precedes creation; standalone resource allocatedSize
// replaces that estimate before publication. Resource release precedes charge
// return. No CPU proxy allocation or driver-wide accounting. Existing callers
// guarantee GPU completion; this helper does not invent a frame-delay policy.
// E.6 (#re-raii), SL.con.3 (#rsl-bounds), GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) apply to
// both consulted phases: bounds, retained ownership and transactional rollback.
#include "platform/macos/texture_allocation.hpp"

#include <new>
#include <stdexcept>
#include <utility>

namespace bw::platform::macos {
TextureAllocation::TextureAllocation(memory::DeviceCharge charge, id<MTLTexture> texture) noexcept
    : charge_(std::move(charge)), texture_(texture) {}
TextureAllocation::~TextureAllocation() { reset(); }
TextureAllocation::TextureAllocation(TextureAllocation&& other) noexcept
    : charge_(std::move(other.charge_)), texture_(other.texture_) {
  other.texture_ = nil;
}
TextureAllocation& TextureAllocation::operator=(TextureAllocation&& other) noexcept {
  if (this != &other) {
    reset();
    charge_ = std::move(other.charge_);
    texture_ = other.texture_;
    other.texture_ = nil;
  }
  return *this;
}
void TextureAllocation::reset() noexcept {
  texture_ = nil;
  charge_.reset();
}
TextureAllocation TextureAllocation::create(id<MTLDevice> device,
                                            const std::shared_ptr<memory::DeviceBudget>& budget,
                                            std::uint32_t width, std::uint32_t height,
                                            MTLPixelFormat format) {
  constexpr std::uint32_t maximum_dimension = 8192;
  constexpr std::size_t channels = 4;
  if (device == nil || !budget || width == 0 || height == 0 || width > maximum_dimension ||
      height > maximum_dimension ||
      (format != MTLPixelFormatBGRA8Unorm && format != MTLPixelFormatRGBA8Unorm)) {
    throw std::invalid_argument("Invalid native color texture request");
  }
  @autoreleasepool {
    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead;
    const auto estimate = [device heapTextureSizeAndAlignWithDescriptor:descriptor];
    const auto logical = static_cast<std::size_t>(width) * height * channels;
    auto charge = budget->reserve(logical, estimate.size);
    id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
    if (texture == nil) throw std::bad_alloc();
    charge.commit(texture.allocatedSize);
    return TextureAllocation(std::move(charge), texture);
  }
}
void TextureAllocation::upload(std::span<const std::uint8_t> bytes) {
  constexpr std::size_t channels = 4;
  if (texture_ == nil || bytes.size() != texture_.width * texture_.height * channels) {
    throw std::invalid_argument("Invalid native color texture upload");
  }
  [texture_ replaceRegion:MTLRegionMake2D(0, 0, texture_.width, texture_.height)
              mipmapLevel:0
                withBytes:bytes.data()
              bytesPerRow:texture_.width * channels];
}
}  // namespace bw::platform::macos
