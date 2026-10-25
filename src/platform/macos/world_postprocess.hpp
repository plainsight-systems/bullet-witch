// Presentation timing is supplied by the engine frame publisher, including
// while world simulation is paused. No target-local wall clock drives exposure.
// Both-phase Core I.1 (#ri-explicit) exposes this dependency; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps the snapshot
// inline. Native HDR scene and display-output pass ownership. The caller
// supplies one command buffer and keeps all GPU work completed before the next
// prepare/reset. begin_scene retains/resizes accounted HDR/depth targets and
// returns a borrowed encoder. Scene depth uses the original reversed-depth
// contract: clear zero, near one, far zero; scene submissions must match. The
// caller draws scene-linear color, ends it, then prepare_output schedules
// original whole-scene RGB filtering followed by luminance/bloom passes.
// encode_display writes exposure and gamma into a borrowed BGRA8
// encoder/viewport; display overlays follow it. Resources and pipelines persist
// across frames. Failure throws without claiming a completed image; no hidden
// submission or synchronization. CPU adaptation is the common
// ExposureController; readback occurs only after the preceding buffer
// completes. Geometry, UI and material policy are outside this owner. Both MCP
// phases: I.11 (#ri-raw) separates borrowed encoders from retained texture
// ownership; GPU.9
// (guidelines/gpu/GPU.9-suballocate-and-alias-transient-gpu-memory.md) retains
// a bounded target pool rather than allocating every frame. Source and pinned
// production reference: world-display-output.md. Fullscreen quad interpolation
// reproduces original D3D9 pixel-center UVs through Metal sample coordinates;
// dimensions are destination extents, including the final display viewport.
#pragma once
#import <Metal/Metal.h>

#include <cstdint>
#include <memory>

#include "bw/memory/device_budget.hpp"
#include "bw/memory/domain.hpp"
#include "bw/render/world_output.hpp"
#include "bw/timing/frame_clock.hpp"
namespace bw::platform::macos {
class WorldPostprocess {
 public:
  WorldPostprocess(id<MTLDevice> device,
                   const std::shared_ptr<memory::Domain>& cpu,
                   std::shared_ptr<memory::DeviceBudget> budget);
  ~WorldPostprocess();
  WorldPostprocess(const WorldPostprocess&) = delete;
  WorldPostprocess& operator=(const WorldPostprocess&) = delete;
  WorldPostprocess(WorldPostprocess&&) = delete;
  WorldPostprocess& operator=(WorldPostprocess&&) = delete;
  void configure(const render::WorldOutputParameters& parameters);
  void reset() noexcept;
  id<MTLRenderCommandEncoder> begin_scene(id<MTLCommandBuffer> command,
                                          std::uint32_t width,
                                          std::uint32_t height);
  void prepare_output(id<MTLCommandBuffer> command, id<MTLLibrary> library,
                      timing::FrameTiming frame);
  void encode_display(id<MTLRenderCommandEncoder> encoder,
                      id<MTLLibrary> library, float opacity);

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::platform::macos
