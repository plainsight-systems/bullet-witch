// Display-output extension: explicit creation usage preserves borrowed access
// (I.11, #ri-raw); retained render targets follow GPU.9
// (guidelines/gpu/GPU.9-suballocate-and-alias-transient-gpu-memory.md),
// retrieved for both output design and implementation phases.
// Render-target callers specify sampled/render usage; R32Float retains original
// luminance precision. Resource usage is fixed at creation.
// Native retained 2D or six-face cube texture plus portable device charge.
// World textures preserve packed half-float RGBA data; no gamma conversion or
// mip generation occurs here. The caller supplies already interpreted pixels.
// Explicit levels are immutable allocation metadata. Payload is level-major,
// then face-major; each level halves extents with a minimum of one. Validate
// the complete payload before any write and charge all levels/faces. Creation
// and upload perform O(levels * faces) work plus byte transfer; no temporary
// allocations. Mip design uses E.6 (#re-raii), I.11 (#ri-raw) and COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md):
// move-only candidates own allocation and charge, input spans only borrow.
// Objective-C++ backend header only. create validates BGRA/RGBA8, RGBA16Float
// or Depth32Float dimensions, admits a heap-size estimate and allocates a
// shared sampled color texture or private depth attachment. It reconciles
// allocatedSize before returning. Depth textures reject CPU upload. Native
// failure/over-budget reconciliation releases both. Upload consumes a borrowed
// tightly packed buffer synchronously; cubes retain six square faces in native
// +X, -X, +Y, -Y, +Z, -Z slice order. Caller must retire all GPU consumers
// before reset/destruction; host waits for completion. Move-only; borrowed
// get() is valid only during this owner's retained lifetime. reset releases
// Metal ownership before accounting. No residency/process usage claim. One
// device allocation per creation, no per-upload allocation. E.6
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
// Strong immutable request metadata keeps level count distinct from usage bits.
struct TextureMipLevels {
  std::uint32_t count{1};
};
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
      std::uint32_t width, std::uint32_t height, MTLPixelFormat format,
      MTLTextureType type = MTLTextureType2D,
      MTLTextureUsage usage = MTLTextureUsageShaderRead,
      TextureMipLevels mips = {});
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
