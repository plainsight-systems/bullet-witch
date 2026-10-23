// Native retained 2D color texture plus portable device charge. Objective-C++
// backend header only. create validates BGRA/RGBA dimensions, admits a
// heap-size estimate, allocates a standalone shared Metal texture, reconciles
// allocatedSize before returning. Native failure/over-budget reconciliation
// releases both. Upload consumes a borrowed tightly packed buffer
// synchronously. Caller must retire all GPU consumers before reset/destruction;
// host waits for completion. Move-only; borrowed get() is valid only during
// this owner's retained lifetime. reset releases Metal ownership before
// accounting. No residency/process usage claim. One device allocation per
// creation, no per-upload allocation. E.6
// (#re-raii), SL.con.3 (#rsl-bounds), GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// retrieved in the two MCP phases; see device-memory-ownership.md for pinned
// references.
#pragma once

#import <Metal/Metal.h>

#include <cstdint>
#include <memory>
#include <span>

#include "bw/memory/device_budget.hpp"

namespace bw::platform::macos {
class TextureAllocation {
 public:
  TextureAllocation() = default;
  ~TextureAllocation();
  TextureAllocation(const TextureAllocation&) = delete;
  TextureAllocation& operator=(const TextureAllocation&) = delete;
  TextureAllocation(TextureAllocation&& other) noexcept;
  TextureAllocation& operator=(TextureAllocation&& other) noexcept;
  // Admit estimated storage, then reconcile the native size before publication.
  static TextureAllocation create(
      id<MTLDevice> device, const std::shared_ptr<memory::DeviceBudget>& budget,
      std::uint32_t width, std::uint32_t height, MTLPixelFormat format);
  void upload(std::span<const std::uint8_t> bytes);
  // Caller retires GPU consumers first; release returns the charge afterward.
  void reset() noexcept;
  // Borrowed native access does not transfer ownership or extend its lifetime.
  id<MTLTexture> get() const noexcept { return texture_; }

 private:
  TextureAllocation(memory::DeviceCharge charge,
                    id<MTLTexture> texture) noexcept;
  memory::DeviceCharge charge_;
  id<MTLTexture> texture_{};
};
}  // namespace bw::platform::macos
