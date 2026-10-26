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
// Output lifetime is separate from scene lifetime: tone toggles preserve
// history and preceding measurements; release_scene_targets retires only scene
// storage and temporary images. reset_exposure_history is the source event
// operation; reset ends renderer/device lifetime. All release/reset calls
// require completed GPU work and the same exclusive caller; no mutable history
// is borrowed out. First use explicitly selects current GPU measurement, a
// target inference from pinned Unreal valid view-state initialization, not
// original first-byte parity. Design/implementation Core E.4
// (#re-design-invariants) validates clear/input before mutation; GPU.9 and
// GPU.1 retain storage and four-byte completed readback. See
// output-lifecycle-redesign.md for the complete source/reference comparison.
// Optional final edge filtering consumes the completed display image. A tone
// pass first writes its original BGRA8 intermediate; without tone, the current
// scene/filter image is sampled directly. The edge pass has its own original
// integer-pixel UV convention, point/clamp predecessor sampling, and fixed
// luminance decision algorithm. Its display intermediate is budgeted, reused,
// and released with scene storage. Core I.1 / GPU.9 were read in both phases;
// activation and exact source boundaries: lighting-final-edge-filter.md.
#pragma once
#import <Metal/Metal.h>

#include <array>
#include <cstdint>
#include <memory>

#include "bw/memory/device_budget.hpp"
#include "bw/memory/domain.hpp"
#include "bw/render/postprocess/world_output.hpp"
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
  void reset_exposure_history() noexcept;
  void release_scene_targets() noexcept;
  void reset() noexcept;
  id<MTLRenderCommandEncoder> begin_scene(
      id<MTLCommandBuffer> command, std::uint32_t width, std::uint32_t height,
      std::array<float, 3> clear_color = {});
  void prepare_output(id<MTLCommandBuffer> command, id<MTLLibrary> library,
                      timing::FrameTiming frame);
  void encode_display(id<MTLRenderCommandEncoder> encoder,
                      id<MTLLibrary> library, float opacity);

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::platform::macos
