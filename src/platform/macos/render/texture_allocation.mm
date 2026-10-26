// Mip allocation preserves the header's level-major payload contract. Validate
// all levels before upload; reserve and reconcile complete native storage before
// publication. Both-phase E.6/I.11 and COPY.5 apply to candidate rollback and
// borrowed input, with O(levels * faces) bookkeeping and no upload allocation.
// Display-output extension: explicit creation usage preserves borrowed access
// (I.11, #ri-raw); retained render targets follow GPU.9
// (guidelines/gpu/GPU.9-suballocate-and-alias-transient-gpu-memory.md),
// retrieved for both output design and implementation phases.
// World extension preserves original decoded RGBA16Float bytes and their eight-
// byte logical texels; no sampling, channel conversion or mip policy is hidden.
// Native texture owner implementation; contract in texture_allocation.hpp.
// Native heap-size estimate precedes creation; standalone resource allocatedSize
// replaces that estimate before publication. Resource release precedes charge
// return. No CPU proxy allocation or driver-wide accounting. Existing callers
// guarantee GPU completion; this helper does not invent a frame-delay policy.
// E.6 (#re-raii), SL.con.3 (#rsl-bounds), GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) apply to
// both consulted phases: bounds, retained ownership and transactional rollback.
#include "texture_allocation.hpp"

#include <algorithm>
#include <bit>
#include <new>
#include <stdexcept>
#include <utility>

namespace bw::platform::macos {
namespace {
std::size_t pixel_bytes(MTLPixelFormat format) {
  constexpr std::size_t half_rgba_bytes = 8;
  return format == MTLPixelFormatRGBA16Float ? half_rgba_bytes : 4;
}
}  // namespace
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
// Release the native resource before returning its accounting charge.
void TextureAllocation::reset() noexcept {
  texture_ = nil;
  charge_.reset();
}
TextureAllocation TextureAllocation::create(id<MTLDevice> device,
                                            const std::shared_ptr<memory::DeviceBudget>& budget,
                                            std::uint32_t width, std::uint32_t height,
                                            MTLPixelFormat format, MTLTextureType type,
                                            MTLTextureUsage usage, TextureMipLevels mips) {
  const auto mip_levels = mips.count;
  constexpr std::uint32_t maximum_dimension = 8192;
  const auto channels = pixel_bytes(format);
  if (device == nil || !budget || width == 0 || height == 0 || width > maximum_dimension ||
      height > maximum_dimension || mip_levels == 0 ||
      mip_levels > static_cast<std::uint32_t>(std::bit_width(std::max(width, height))) ||
      (format == MTLPixelFormatDepth32Float && mip_levels != 1) ||
      (type != MTLTextureType2D && type != MTLTextureTypeCube) ||
      (type == MTLTextureTypeCube && (width != height || format == MTLPixelFormatDepth32Float)) ||
      (format != MTLPixelFormatBGRA8Unorm && format != MTLPixelFormatRGBA8Unorm &&
       format != MTLPixelFormatRGBA16Float && format != MTLPixelFormatR32Float &&
       format != MTLPixelFormatDepth32Float)) {
    throw std::invalid_argument("Invalid native color texture request");
  }
  @autoreleasepool {
    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    descriptor.textureType = type;
    descriptor.mipmapLevelCount = mip_levels;
    constexpr std::size_t kCubeFaces = 6;
    const std::size_t faces = type == MTLTextureTypeCube ? kCubeFaces : 1;
    const bool depth = format == MTLPixelFormatDepth32Float;
    descriptor.storageMode = depth ? MTLStorageModePrivate : MTLStorageModeShared;
    descriptor.usage = depth ? MTLTextureUsageRenderTarget : usage;
    // Reserve before native acquisition so failure unwinds both storage and charge.
    const auto estimate = [device heapTextureSizeAndAlignWithDescriptor:descriptor];
    std::size_t logical = 0;
    auto level_width = width, level_height = height;
    for (std::uint32_t level = 0; level < mip_levels; ++level) {
      logical += static_cast<std::size_t>(level_width) * level_height * channels * faces;
      level_width = std::max(level_width / 2, 1U);
      level_height = std::max(level_height / 2, 1U);
    }
    auto charge = budget->reserve(logical, estimate.size);
    id<MTLTexture> texture = [device newTextureWithDescriptor:descriptor];
    if (texture == nil) throw std::bad_alloc();
    // Replace the estimate with the driver-reported size before returning ownership.
    charge.commit(texture.allocatedSize);
    return TextureAllocation(std::move(charge), texture);
  }
}
void TextureAllocation::upload(std::span<const std::uint8_t> bytes) {
  if (texture_ == nil || texture_.pixelFormat == MTLPixelFormatDepth32Float) {
    throw std::invalid_argument("Invalid native color texture upload");
  }
  const auto channels = pixel_bytes(texture_.pixelFormat);
  constexpr std::size_t kCubeFaces = 6;
  const std::size_t faces = texture_.textureType == MTLTextureTypeCube ? kCubeFaces : 1;
  std::size_t expected = 0;
  for (NSUInteger level = 0; level < texture_.mipmapLevelCount; ++level) {
    expected += std::max(texture_.width >> level, NSUInteger{1}) *
                std::max(texture_.height >> level, NSUInteger{1}) * channels * faces;
  }
  if (bytes.size() != expected) {
    throw std::invalid_argument("Invalid native color texture upload");
  }
  // Validate the entire chain before touching even level zero. Original cube
  // faces retain their order inside each complete level.
  std::size_t offset = 0;
  for (NSUInteger level = 0; level < texture_.mipmapLevelCount; ++level) {
    const auto width = std::max(texture_.width >> level, NSUInteger{1});
    const auto height = std::max(texture_.height >> level, NSUInteger{1});
    const auto face_bytes = width * height * channels;
    for (std::size_t face = 0; face < faces; ++face) {
      [texture_ replaceRegion:MTLRegionMake2D(0, 0, width, height)
                  mipmapLevel:level
                        slice:face
                    withBytes:bytes.subspan(offset, face_bytes).data()
                  bytesPerRow:width * channels
                bytesPerImage:face_bytes];
      offset += face_bytes;
    }
  }
}
}  // namespace bw::platform::macos
