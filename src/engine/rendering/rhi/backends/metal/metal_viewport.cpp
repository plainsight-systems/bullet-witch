#include "metal_viewport.hpp"

#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/viewport.hpp"

#include "metal_formats.hpp"
#include "metal_objects.hpp"

#include <cstdint>
#include <utility>

namespace bw::rhi::metal {

namespace {

[[nodiscard]] CGSize drawable_size(Extent2 size) noexcept {
  return CGSize{static_cast<CGFloat>(size.width), static_cast<CGFloat>(size.height)};
}

} // namespace

// The layer is configured for this device and the description, then the
// first back buffer is made. The layer's drawable size is in pixels, which
// is what the description's size is (platform::Window::pixel_size).
MetalViewport::MetalViewport(DeferredDeleteQueue& queue, const ViewportDescription& description,
                             MTL::Device& device)
    : Viewport(queue, description),
      layer_(static_cast<CA::MetalLayer*>(description.window.surface)) {
  const AutoreleaseScope pool;
  layer_->retain();
  layer_->setDevice(&device);
  layer_->setPixelFormat(pixel_format(description.format));
  layer_->setFramebufferOnly(false);
  layer_->setDrawableSize(drawable_size(description.pixel_size));
  layer_->setDisplaySyncEnabled(description.vsync);
  replace_back_buffer(description.pixel_size);
}

// The current back buffer reaches no viewport after this; the base releases
// it into the delete queue. A held drawable cannot outlive its frame's
// submit (take_drawable), and the viewport is destroyed only after that
// frame completed; a drawable still held is released unpresented.
MetalViewport::~MetalViewport() {
  const AutoreleaseScope pool;
  back_buffer_texture().detach_viewport();
  if (drawable_ != nullptr) {
    drawable_->release();
  }
  layer_->release();
}

MetalTexture& MetalViewport::back_buffer_texture() const noexcept {
  return static_cast<MetalTexture&>(back_buffer());
}

// The caller detaches the old back buffer first (resize); at construction
// there is none.
void MetalViewport::replace_back_buffer(Extent2 size) {
  Ref<Texture> next(new MetalTexture(
      delete_queue(), back_buffer_description(size, format(), description().debug_name), *this));
  set_back_buffer(std::move(next), size);
}

// viewport.hpp, decide_acquire and after_acquire, with nextDrawable as the
// surface.
// A drawable with no texture is a drawable the surface could not give, so it
// is BackBufferUnavailable, checked before the size (viewport.hpp,
// invariant 4); reading its size would report a 0x0 mismatch instead.
// A drawable of the wrong size is not exchanged for another: the mismatch is
// BackBufferSizeMismatch and the host resizes (viewport.hpp, invariant 4),
// because the drawable size changes only through resize_viewport.
ExecuteError MetalViewport::acquire(std::uint64_t frame) {
  if (decide_acquire(frame_state(), frame) == AcquireDecision::Reuse) {
    return ExecuteError::None;
  }
  const AutoreleaseScope pool;
  CA::MetalDrawable* drawable = layer_->nextDrawable();
  if (drawable == nullptr) {
    return ExecuteError::BackBufferUnavailable;
  }
  MTL::Texture* texture = drawable->texture();
  if (texture == nullptr) {
    // The autoreleased drawable is dropped with the pool, never presented.
    return ExecuteError::BackBufferUnavailable;
  }
  const AcquireResult result =
      after_acquire(frame_state(), frame,
                    Extent2{static_cast<std::uint32_t>(texture->width()),
                            static_cast<std::uint32_t>(texture->height())});
  if (result.outcome == AcquireOutcome::SizeMismatch) {
    // The autoreleased drawable is dropped with the pool, never presented.
    return ExecuteError::BackBufferSizeMismatch;
  }
  drawable->retain();
  drawable_ = drawable;
  set_frame_state(result.state);
  back_buffer_texture().set_drawable_texture(texture);
  return ExecuteError::None;
}

// viewport.hpp, presents_at_end_of_frame and after_frame_end. The same call
// serves the context's unwind of a pass whose later back buffer failed to
// acquire (metal_context.hpp, Back buffers): the caller releases the
// drawable unpresented.
CA::MetalDrawable* MetalViewport::take_drawable(std::uint64_t frame) noexcept {
  if (!presents_at_end_of_frame(frame_state(), frame)) {
    return nullptr;
  }
  set_frame_state(after_frame_end(frame_state()));
  back_buffer_texture().set_drawable_texture(nullptr);
  return std::exchange(drawable_, nullptr);
}

// The device waited idle before this and refused a resize while the back
// buffer is acquired in the frame being recorded (device.hpp,
// resize_viewport; viewport.hpp, validate_resize), so no drawable is held
// here. The layer's drawable size is set before the back buffer is
// replaced, so the next acquire returns a drawable of the new size. The
// format is fixed (viewport.hpp, invariant 7) and the size is already in
// pixels, so neither is adjusted here.
void MetalViewport::resize(Extent2 size) {
  const AutoreleaseScope pool;
  layer_->setDrawableSize(drawable_size(size));
  back_buffer_texture().detach_viewport();
  replace_back_buffer(size);
  set_frame_state(after_resize(frame_state(), size));
}

void MetalViewport::apply_vsync(bool vsync) {
  const AutoreleaseScope pool;
  layer_->setDisplaySyncEnabled(vsync);
  set_vsync_state(vsync);
}

} // namespace bw::rhi::metal
