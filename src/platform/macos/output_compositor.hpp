// Native final output authority: world/postprocess, then movie, then ordered
// UI. Borrowed producer owners outlive this object and cannot be updated during
// composition. The layer belongs to the window host; it may be nil for native
// offscreen use. The compositor owns each command until completed, including
// presentation, so every return permits producer storage reuse/retirement.
// Retained native queue/library/layer handles are ARC-managed. No independent
// timing/audio update, framebuffer copy, async worker or render graph is added.
// present returns false without changing producers when no drawable exists;
// compose writes a borrowed BGRA8 target and returns after completion. Inputs
// are validated before encoding; native failures throw. Main thread only.
// Canvas placement preserves the existing pixel-aligned logical viewport;
// the movie renderer independently preserves source aspect ratio. Costs scale
// with submitted world/UI work; images remain on device, existing world
// luminance readback is unchanged. See output-composition.md.
// Both design and implementation retrieved Core R.1 (#rr-raii), I.11
// (#ri-raw), performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md), and GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md): stable resource owners,
// explicit borrowed encoders, no intermediate CPU copies. Native ARC handles
// and budgeted allocations release automatically after synchronous completion.
#pragma once
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "bw/render/output_compositor.hpp"
#include "platform/macos/movie_renderer.hpp"
#include "platform/macos/sprite_renderer.hpp"
#include "platform/macos/world_renderer.hpp"
namespace bw::platform::macos {
class OutputCompositor final : public render::OutputCompositor {
 public:
  OutputCompositor(id<MTLCommandQueue> commands, id<MTLLibrary> library,
                   CAMetalLayer* layer, WorldRenderer& world,
                   const MovieRenderer& movie, const SpriteRenderer& sprites);
  ~OutputCompositor() override = default;
  OutputCompositor(const OutputCompositor&) = delete;
  OutputCompositor& operator=(const OutputCompositor&) = delete;
  OutputCompositor(OutputCompositor&&) = delete;
  OutputCompositor& operator=(OutputCompositor&&) = delete;
  bool present(const render::OutputFrame& frame) override;
  // Same submission path with a borrowed native target, without window/display.
  void compose(id<MTLTexture> target, const render::OutputFrame& frame);

 private:
  void validate(const render::OutputFrame& frame) const;
  void submit(id<MTLTexture> target, id<CAMetalDrawable> drawable,
              const render::OutputFrame& frame);
  id<MTLCommandQueue> commands_;
  id<MTLLibrary> library_;
  CAMetalLayer* layer_;
  WorldRenderer& world_;
  const MovieRenderer& movie_;
  const SpriteRenderer& sprites_;
};
}  // namespace bw::platform::macos
