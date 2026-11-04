#ifndef BW_RENDERING_RHI_VIEWPORT_HPP
#define BW_RENDERING_RHI_VIEWPORT_HPP
#include "bw/platform/window/window.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/build.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"

#include <cstdint>
#include <string_view>
#include <utility>

// Responsibility: the viewport (docs/architecture/rhi.md, "Scope by game
// need": swapchain and present; invariant 8 and "Frame flow": the back
// buffer is acquired when a pass first targets it and present is enqueued
// with the frame's commands; the end of frame is the single boundary). One
// viewport per window, created by the device from the window's native
// handle (platform/window/window.hpp) and a description; it owns the
// output surface at the window's pixel size and one back buffer texture.
//
// Invariants, each stated for the tests.
//   1. The viewport never dictates render size. It owns the output surface
//      at the window's pixel size; a renderer owns its scene targets at a
//      size of its own and writes to the back buffer in its final pass.
//      Nothing here is read to size a scene target.
//   2. back_buffer() is one Texture of size(), format(), usage RenderTarget
//      and CopySource with the BackBuffer mark, never sampled. Its tracked
//      access is Present when the viewport is created and after every
//      resize. It is acquired from the surface by the first render pass of
//      a frame that binds it (command_list.hpp, begin_render_pass) and
//      released at that frame's end_frame; a copy from it before any pass of
//      the frame bound it is a validation failure (BackBufferNotAcquired).
//   3. Present happens at the frame boundary: end_frame presents every back
//      buffer acquired in that frame, on the frame's own command buffer,
//      after the frame's work. The caller declares the transition to Present
//      before end_frame; an acquired back buffer whose tracked access is not
//      Present at end_frame is a validation failure (BackBufferNotPresentable)
//      and is presented all the same. A back buffer acquired and never
//      presented does not exist: acquisition implies presentation at the
//      boundary.
//   4. Acquisition fails by name: when the surface has no drawable to give,
//      begin_render_pass returns ExecuteError::BackBufferUnavailable, the
//      replay stops there and the frame is not drawn; when the drawable's
//      size differs from size(), BackBufferSizeMismatch, and the host resizes
//      the viewport. A pass that fails to acquire any of its back buffers
//      holds none of the back buffers it acquired: they are released
//      unpresented and the frame may resize them. There is no substitute
//      texture and no silent skip.
//   5. Resize is explicit: the host calls Device::resize_viewport from a
//      window event with the window's pixel size. The device waits for every
//      submitted frame, resizes the surface and replaces the back buffer:
//      back_buffer() then refers to a new Texture in Present access, of the
//      new size, and the old one goes to the delete queue. A resize while
//      the back buffer is acquired in the frame being recorded is refused,
//      ResizeDuringFrame, and changes nothing.
//   6. vsync is a description field applied to the surface and reported
//      back by vsync(); set_vsync changes it between frames and reports the
//      new value. Frames in flight stay at the device's bound; the surface's
//      own drawable count is not a second bound this header states.
//   7. Format is B8G8R8A8UnormSrgb this slice; any other is
//      FormatUnsupported. No HDR, no color space selection.
//
// The pure bookkeeping below (ViewportFrameState and its functions) is
// what a backend's viewport drives, with no API; it is unit tested, and
// the backend keeps one probe that the path is wired (AGENTS.md, Testing).
//
// Invariant 4 has no substitute texture because a frame drawn into a
// texture no one sees is a hidden failure. A size mismatch holds nothing and
// is not answered by a second acquire: the sizes differ only when the
// surface was resized behind the viewport, which the host answers with
// resize_viewport. The game renders its final pass into the drawable, with
// no intermediate back buffer. vsync on the surface paces the game and the
// device bounds frames in flight; there is no separate frame pacer. vsync is
// the description's value in every window mode.
//
// Core guidelines read: C.35 and C.67 (through Resource), I.1 and E.27
// (the description is complete; failure is a named code), C.32 (the native
// handle is a borrowed surface the viewport retains through the API's own
// reference count). Performance guidelines read: GPU.7 (the back buffer is
// one more in-flight resource, bounded with the frames; the only blocking
// wait is the explicit resize), MEM.4 (acquire and release at the frame's
// one flip point).
namespace bw::rhi {

struct ViewportDescription {
  platform::NativeWindowHandle window{};
  // The window's content area in pixels (platform::Window::pixel_size).
  Extent2 pixel_size{};
  Format format{Format::B8G8R8A8UnormSrgb};
  bool vsync{true};
  std::string_view debug_name{};
};

// Named failures, the first in this order that applies.
enum class ViewportError : std::uint8_t {
  None,
  // kind None or a null surface.
  NoSurface,
  // The backend takes another surface kind (Capabilities::surface_kind).
  SurfaceKindUnsupported,
  ZeroSize,
  // Not B8G8R8A8UnormSrgb (invariant 7).
  FormatUnsupported,
  MissingDebugName,
  // Resize while the back buffer is acquired in the frame being recorded.
  ResizeDuringFrame,
  // The surface refused the device, format or size; detail carries the
  // backend's text.
  BackendRejected,
};

// The pure checks of Device::create_viewport, in ViewportError's order:
// NoSurface, ZeroSize, FormatUnsupported, MissingDebugName. The overload
// with the backend's surface kind (Capabilities::surface_kind) is what the
// device calls: NoSurface, then SurfaceKindUnsupported, then the rest.
[[nodiscard]] constexpr ViewportError validate_viewport(const ViewportDescription& d) noexcept {
  if (d.window.kind == platform::NativeSurfaceKind::None || d.window.surface == nullptr) {
    return ViewportError::NoSurface;
  }
  if (d.pixel_size.width == 0U || d.pixel_size.height == 0U) {
    return ViewportError::ZeroSize;
  }
  if (d.format != Format::B8G8R8A8UnormSrgb) {
    return ViewportError::FormatUnsupported;
  }
  if (d.debug_name.empty()) {
    return ViewportError::MissingDebugName;
  }
  return ViewportError::None;
}
[[nodiscard]] constexpr ViewportError
validate_viewport(const ViewportDescription& d, platform::NativeSurfaceKind backend_kind) noexcept {
  if (d.window.kind == platform::NativeSurfaceKind::None || d.window.surface == nullptr) {
    return ViewportError::NoSurface;
  }
  if (d.window.kind != backend_kind) {
    return ViewportError::SurfaceKindUnsupported;
  }
  return validate_viewport(d);
}

// The back buffer's description for a viewport of `size` and `format`:
// RenderTarget | CopySource | BackBuffer, initial access Present, clear
// value opaque black, one mip, one slice. The debug name is the viewport's.
[[nodiscard]] constexpr TextureDescription back_buffer_description(Extent2 size, Format format,
                                                                   std::string_view name) noexcept {
  TextureDescription d{};
  d.extent = Extent3{size.width, size.height, 1};
  d.format = format;
  d.usage = TextureUsage::RenderTarget | TextureUsage::CopySource | TextureUsage::BackBuffer;
  d.initial_access = Access::Present;
  d.clear_value.color = {0.0F, 0.0F, 0.0F, 1.0F};
  d.debug_name = name;
  return d;
}

// ---- Pure frame bookkeeping ---------------------------------------------------

// What the viewport knows between API calls: the back buffer's size, and
// the frame in which it was acquired (0: not held).
struct ViewportFrameState {
  Extent2 size{};
  std::uint64_t acquired_frame{};
  [[nodiscard]] friend constexpr bool operator==(ViewportFrameState,
                                                 ViewportFrameState) noexcept = default;
};

// At a render pass that binds the back buffer in `frame`: acquire once per
// frame, reuse the held drawable after that (invariant 2).
enum class AcquireDecision : std::uint8_t { Acquire, Reuse };
[[nodiscard]] constexpr AcquireDecision decide_acquire(ViewportFrameState state,
                                                       std::uint64_t frame) noexcept {
  return state.acquired_frame == frame ? AcquireDecision::Reuse : AcquireDecision::Acquire;
}

// After the surface gave a drawable of `drawable` pixels in `frame`: the
// state holds it when the size matches; a mismatch holds nothing and the
// caller returns BackBufferSizeMismatch (invariant 4).
enum class AcquireOutcome : std::uint8_t { Held, SizeMismatch };
struct AcquireResult {
  ViewportFrameState state{};
  AcquireOutcome outcome{AcquireOutcome::Held};
};
[[nodiscard]] constexpr AcquireResult after_acquire(ViewportFrameState state, std::uint64_t frame,
                                                    Extent2 drawable) noexcept {
  if (drawable != state.size) {
    return AcquireResult{state, AcquireOutcome::SizeMismatch};
  }
  state.acquired_frame = frame;
  return AcquireResult{state, AcquireOutcome::Held};
}

// Whether end_frame of `frame` presents this viewport (invariant 3), and the
// state after the boundary: nothing held.
[[nodiscard]] constexpr bool presents_at_end_of_frame(ViewportFrameState state,
                                                      std::uint64_t frame) noexcept {
  return state.acquired_frame == frame;
}
[[nodiscard]] constexpr ViewportFrameState after_frame_end(ViewportFrameState state) noexcept {
  state.acquired_frame = 0;
  return state;
}

// Resize in the frame being recorded (invariant 5): refused while the back
// buffer is acquired in it; otherwise the new size takes effect and nothing
// is held, so the next pass acquires a drawable of the new size.
[[nodiscard]] constexpr ViewportError
validate_resize(ViewportFrameState state, std::uint64_t recording_frame, Extent2 size) noexcept {
  if (size.width == 0U || size.height == 0U) {
    return ViewportError::ZeroSize;
  }
  if (state.acquired_frame == recording_frame) {
    return ViewportError::ResizeDuringFrame;
  }
  return ViewportError::None;
}
[[nodiscard]] constexpr ViewportFrameState after_resize(ViewportFrameState state,
                                                        Extent2 size) noexcept {
  state.size = size;
  state.acquired_frame = 0;
  return state;
}

// ---- The viewport ------------------------------------------------------------------

// Created by the device only (device.hpp, create_viewport); backends derive
// it. The host holds a Ref; the back buffer's Ref is the viewport's and
// changes at resize.
class Viewport : public Resource {
public:
  // description().debug_name views this object's copy of the name
  // (resources.hpp, DebugName: empty with BW_RHI_DEBUG 0).
  [[nodiscard]] const ViewportDescription& description() const noexcept { return description_; }
  // Precondition: creation succeeded; never null. The object changes at
  // resize (invariant 5).
  [[nodiscard]] Texture& back_buffer() const noexcept { return *back_buffer_; }
  [[nodiscard]] Extent2 size() const noexcept { return description_.pixel_size; }
  [[nodiscard]] Format format() const noexcept { return description_.format; }
  [[nodiscard]] bool vsync() const noexcept { return description_.vsync; }
  // The pure bookkeeping above as the backend last left it; frame_state().size
  // equals size(). The device reads it for validate_resize (invariant 5).
  [[nodiscard]] ViewportFrameState frame_state() const noexcept { return state_; }

protected:
  // Starts with frame_state() of the description's size and nothing held.
  Viewport(DeferredDeleteQueue& queue, const ViewportDescription& description) noexcept;
  // For the backend's acquire, present, resize and vsync paths.
  void set_frame_state(ViewportFrameState state) noexcept { state_ = state; }
  void set_back_buffer(Ref<Texture> back_buffer, Extent2 size) noexcept {
    back_buffer_ = std::move(back_buffer);
    description_.pixel_size = size;
    state_.size = size;
  }
  void set_vsync_state(bool vsync) noexcept { description_.vsync = vsync; }

private:
  ViewportDescription description_;
  DebugName name_;
  Ref<Texture> back_buffer_{};
  ViewportFrameState state_{};
};

using ViewportResult = Result<Ref<Viewport>, ViewportError>;

} // namespace bw::rhi
#endif
