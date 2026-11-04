#ifndef BW_RENDERING_RHI_BACKENDS_METAL_METAL_VIEWPORT_HPP
#define BW_RENDERING_RHI_BACKENDS_METAL_METAL_VIEWPORT_HPP
// The Metal viewport (viewport.hpp): the window's CAMetalLayer, configured
// for this device, and the back buffer, a MetalTexture whose Metal texture
// is the drawable's while the frame holds one.
//
// Contract (viewport.hpp's, with what Metal adds).
//   Layer: the window's CAMetalLayer (platform/window/window.hpp,
//   NativeSurfaceKind::MetalLayer), retained for the viewport's life. At
//   creation the viewport sets its device, pixel format BGRA8Unorm_sRGB,
//   framebufferOnly NO (the back buffer is a copy source), drawable size
//   (pixels) and displaySyncEnabled from vsync; resize sets the drawable
//   size, set_vsync displaySyncEnabled.
//   Acquire: the context calls acquire(frame) at a render pass that binds
//   the back buffer; the first call of a frame takes nextDrawable, a later
//   one reuses it (decide_acquire). No drawable, or a drawable with no
//   texture, is BackBufferUnavailable; a drawable whose texture size is not
//   size() is BackBufferSizeMismatch, and the drawable is dropped
//   (after_acquire). A held drawable is retained by the viewport and its
//   texture is the back buffer's texture().
//   Present: the backend takes the drawable at submit_frame of the frame
//   that acquired it (take_drawable, presents_at_end_of_frame) and presents
//   it on that frame's command buffer; the viewport then holds nothing
//   (after_frame_end). The context also takes it, and releases it
//   unpresented, when another back buffer of the same pass failed to
//   acquire (metal_context.hpp, Back buffers).
//   Threads: every call is on the one thread that owns the window and the
//   submission (device.hpp, Viewports).
//
// The window creates and configures the layer it hosts
// (platform/os/macos/window.hpp); the viewport sets only what rendering into
// it needs. displaySyncEnabled is written when vsync is set, the only time
// it changes (viewport.hpp, invariant 6), not at every present.
//
// Core guidelines read: C.35 (destroyed through the delete queue), R.1 (the
// layer and a held drawable each have one owner that releases them), C.32
// (the back buffer's back-pointer is an observer the viewport clears).
// Performance guidelines read: GPU.7 (one drawable held per frame, released
// at the frame boundary).
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/viewport.hpp"

#include "metal_api.hpp"

#include <QuartzCore/QuartzCore.hpp>

#include <cstdint>

namespace bw::rhi::metal {

class MetalTexture;

class MetalViewport final : public Viewport {
public:
  // Preconditions: the device validated the description (viewport.hpp,
  // validate_viewport) and its surface is a CAMetalLayer. The layer is
  // retained here and released by the destructor.
  MetalViewport(DeferredDeleteQueue& queue, const ViewportDescription& description,
                MTL::Device& device);

  // At a render pass of `frame` that binds the back buffer; the contract.
  [[nodiscard]] ExecuteError acquire(std::uint64_t frame);
  // At submit_frame(frame), or at the unwind of a failed pass of that frame:
  // the drawable acquired in that frame, owned by the caller (+1), or null
  // when the frame acquired none. The viewport holds nothing after it.
  [[nodiscard]] CA::MetalDrawable* take_drawable(std::uint64_t frame) noexcept;
  // backend.hpp, resize_viewport: the layer's drawable size, then a new back
  // buffer of `size`; the old one goes to the delete queue.
  void resize(Extent2 size);
  void apply_vsync(bool vsync);

private:
  ~MetalViewport() override;
  [[nodiscard]] MetalTexture& back_buffer_texture() const noexcept;
  // A new back buffer of `size` in Present access; the Ref the base held on
  // the old one, if any, is released into the delete queue.
  void replace_back_buffer(Extent2 size);

  CA::MetalLayer* layer_;
  // Retained while held (between acquire and take_drawable). The frame
  // bookkeeping is the base's (Viewport::frame_state, set_frame_state).
  CA::MetalDrawable* drawable_{};
};

} // namespace bw::rhi::metal
#endif
