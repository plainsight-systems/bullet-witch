// Headless native Metal resource contract within the existing memory-tests
// scenario. No Cocoa application/window, files or proprietary assets. Exercise
// retained textures through an actual command-buffer completion, compare bytes,
// verify reported native accounting and deterministic budget rejection. Device
// absence is a meaningful failure, not a skipped pass. CPU readback is diagnostic
// only. E.6 (#re-raii), SL.con.3 (#rsl-bounds), GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) from both
// MCP phases preserve ownership until GPU completion and bounded test storage.
#include <array>
#include <cstdint>
#include <stdexcept>

#include "platform/macos/cpu_backing.hpp"
#include "platform/macos/texture_allocation.hpp"

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}  // namespace
void BWMacosTextureAllocationTests() {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    require(device != nil, "No Metal device for native memory acceptance");
    auto domain = bw::memory::Domain::create(bw::platform::macos::make_cpu_backing());
    constexpr std::uint32_t width = 32, height = 16;
    constexpr std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
    auto budget = bw::memory::DeviceBudget::create(domain, {.max_logical_bytes = 2 * bytes});
    std::array<std::uint8_t, bytes> pixels{};
    pixels.fill(0x5a);
    auto source = bw::platform::macos::TextureAllocation::create(device, budget, width, height,
                                                                 MTLPixelFormatRGBA8Unorm);
    source.upload(pixels);
    require(budget->snapshot().live_logical_bytes == bytes &&
                budget->snapshot().live_native_bytes == source.get().allocatedSize &&
                budget->snapshot().pending_resources == 0,
            "Texture reports allocated native bytes separately from logical size");
    auto destination = bw::platform::macos::TextureAllocation::create(device, budget, width, height,
                                                                      MTLPixelFormatRGBA8Unorm);
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLBlitCommandEncoder> encoder = [command blitCommandEncoder];
    require(queue != nil && command != nil && encoder != nil, "Native texture command creation");
    [encoder copyFromTexture:source.get()
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(width, height, 1)
                   toTexture:destination.get()
            destinationSlice:0
            destinationLevel:0
           destinationOrigin:MTLOriginMake(0, 0, 0)];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    require(command.status == MTLCommandBufferStatusCompleted, "Native texture consumer completed");
    std::array<std::uint8_t, bytes> result{};
    [destination.get() getBytes:result.data()
                    bytesPerRow:static_cast<NSUInteger>(width) * 4
                     fromRegion:MTLRegionMake2D(0, 0, width, height)
                    mipmapLevel:0];
    require(result == pixels, "GPU consumer preserves uploaded texture data");
    bool rejected = false;
    try {
      source = bw::platform::macos::TextureAllocation::create(device, budget, width, height,
                                                              MTLPixelFormatRGBA8Unorm);
    } catch (const std::bad_alloc&) {
      rejected = true;
    }
    require(rejected && source.get() != nil && budget->snapshot().live_resources == 2 &&
                budget->snapshot().pending_resources == 0,
            "Rejected replacement retains prior texture and accounting");
    auto limited = bw::memory::DeviceBudget::create(domain, {.max_logical_bytes = bytes - 1});
    rejected = false;
    try {
      (void)bw::platform::macos::TextureAllocation::create(device, limited, width, height,
                                                           MTLPixelFormatRGBA8Unorm);
    } catch (const std::bad_alloc&) {
      rejected = true;
    }
    require(rejected && limited->snapshot().pending_resources == 0 &&
                limited->snapshot().live_resources == 0,
            "Native texture admission rejection leaves no charge");
    // Command references are retired before releasing texture owners/charges.
    encoder = nil;
    command = nil;
    source.reset();
    destination.reset();
    require(budget->snapshot().live_resources == 0 && budget->snapshot().live_native_bytes == 0,
            "Completed native texture owners return all charges");
  }
}
