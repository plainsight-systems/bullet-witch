// Unit test: public contracts of bw/rendering/rhi/viewport.hpp, one scenario
// per CTest entry: the description checks, the back buffer's description and
// the pure frame bookkeeping a backend's viewport drives, and the state a
// Viewport starts with. Each scenario names the header declaration it
// asserts.
#include "bw/rendering/rhi/viewport.hpp"

#include "bw/platform/window/window.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/build.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <string>

namespace {
using bw::rhi::AcquireDecision;
using bw::rhi::AcquireOutcome;
using bw::rhi::Extent2;
using bw::rhi::Format;
using bw::rhi::TextureUsage;
using bw::rhi::ViewportDescription;
using bw::rhi::ViewportError;
using bw::rhi::ViewportFrameState;
using bw::testing::require;
using Scenario = bw::testing::Scenario<void>;

constexpr Extent2 kSize{1280, 720};
constexpr Extent2 kResized{1920, 1080};

ViewportDescription valid_description(void* surface) {
  ViewportDescription d{};
  d.window = bw::platform::NativeWindowHandle{bw::platform::NativeSurfaceKind::MetalLayer, surface};
  d.pixel_size = kSize;
  d.debug_name = "Main viewport";
  return d;
}

// (viewport.hpp, validate_viewport) and (viewport.hpp, ViewportError): a
// kind of None or a null surface is NoSurface, a zero dimension ZeroSize, a
// format other than B8G8R8A8UnormSrgb FormatUnsupported (invariant 7), an
// empty name MissingDebugName; the first in that order is returned.
void validate_viewport_named_in_order() {
  int surface = 0;
  require(bw::rhi::validate_viewport(valid_description(&surface)) == ViewportError::None,
          "valid description");
  ViewportDescription d = valid_description(&surface);
  d.window.kind = bw::platform::NativeSurfaceKind::None;
  d.pixel_size = Extent2{};
  require(bw::rhi::validate_viewport(d) == ViewportError::NoSurface,
          "kind None is NoSurface, before ZeroSize");
  d = valid_description(nullptr);
  require(bw::rhi::validate_viewport(d) == ViewportError::NoSurface, "a null surface");
  d = valid_description(&surface);
  d.pixel_size.height = 0;
  d.format = Format::R8G8B8A8Unorm;
  require(bw::rhi::validate_viewport(d) == ViewportError::ZeroSize,
          "ZeroSize before FormatUnsupported");
  d = valid_description(&surface);
  d.format = Format::B8G8R8A8Unorm;
  d.debug_name = {};
  require(bw::rhi::validate_viewport(d) == ViewportError::FormatUnsupported,
          "FormatUnsupported before MissingDebugName");
  d = valid_description(&surface);
  d.debug_name = {};
  require(bw::rhi::validate_viewport(d) == ViewportError::MissingDebugName, "MissingDebugName");
}

// (viewport.hpp, validate_viewport, the overload with the backend's surface
// kind): NoSurface first, even when the kinds agree or the kind is the
// backend's; then SurfaceKindUnsupported, before ZeroSize; then the
// one-argument order. The enumeration has one surface kind, so the
// mismatch is a MetalLayer surface offered to a backend whose kind is None.
void validate_with_backend_kind() {
  using bw::platform::NativeSurfaceKind;
  int surface = 0;
  require(bw::rhi::validate_viewport(valid_description(&surface), NativeSurfaceKind::MetalLayer) ==
              ViewportError::None,
          "a valid description of the backend's kind is None");

  ViewportDescription d = valid_description(nullptr);
  require(bw::rhi::validate_viewport(d, NativeSurfaceKind::MetalLayer) == ViewportError::NoSurface,
          "a null surface of the backend's kind is NoSurface");
  d = valid_description(&surface);
  d.window.kind = NativeSurfaceKind::None;
  require(bw::rhi::validate_viewport(d, NativeSurfaceKind::None) == ViewportError::NoSurface,
          "kind None is NoSurface, even when it equals the backend's kind");
  d = valid_description(nullptr);
  d.pixel_size = Extent2{};
  require(bw::rhi::validate_viewport(d, NativeSurfaceKind::None) == ViewportError::NoSurface,
          "NoSurface before SurfaceKindUnsupported and ZeroSize");

  d = valid_description(&surface);
  require(bw::rhi::validate_viewport(d, NativeSurfaceKind::None) ==
              ViewportError::SurfaceKindUnsupported,
          "a surface of another kind than the backend's is SurfaceKindUnsupported");
  d.pixel_size = Extent2{};
  d.format = Format::R8G8B8A8Unorm;
  d.debug_name = {};
  require(bw::rhi::validate_viewport(d, NativeSurfaceKind::None) ==
              ViewportError::SurfaceKindUnsupported,
          "SurfaceKindUnsupported before ZeroSize, FormatUnsupported and MissingDebugName");

  d = valid_description(&surface);
  d.pixel_size.width = 0;
  d.format = Format::R8G8B8A8Unorm;
  require(bw::rhi::validate_viewport(d, NativeSurfaceKind::MetalLayer) == ViewportError::ZeroSize,
          "then ZeroSize before FormatUnsupported");
  d = valid_description(&surface);
  d.format = Format::B8G8R8A8Unorm;
  d.debug_name = {};
  require(bw::rhi::validate_viewport(d, NativeSurfaceKind::MetalLayer) ==
              ViewportError::FormatUnsupported,
          "then FormatUnsupported before MissingDebugName");
  d = valid_description(&surface);
  d.debug_name = {};
  require(bw::rhi::validate_viewport(d, NativeSurfaceKind::MetalLayer) ==
              ViewportError::MissingDebugName,
          "then MissingDebugName");
}

// (viewport.hpp, back_buffer_description) and invariant 2: a render target
// and copy source with the BackBuffer mark, never sampled, of the given size
// and format, in Present access, one mip and one slice, cleared to opaque
// black, named by the viewport.
void back_buffer_description() {
  const bw::rhi::TextureDescription d =
      bw::rhi::back_buffer_description(kSize, Format::B8G8R8A8UnormSrgb, "Main viewport");
  require(d.extent.width == kSize.width && d.extent.height == kSize.height && d.extent.depth == 1U,
          "the viewport's size");
  require(d.format == Format::B8G8R8A8UnormSrgb, "the viewport's format");
  require(d.usage ==
              (TextureUsage::RenderTarget | TextureUsage::CopySource | TextureUsage::BackBuffer),
          "render target, copy source and the back buffer mark");
  require(!bw::rhi::has_usage(d.usage, TextureUsage::ShaderResource), "never sampled");
  require(d.initial_access == bw::rhi::Access::Present, "Present at creation");
  require(d.mip_levels == 1U && d.array_size == 1U, "one mip, one slice");
  constexpr std::array<float, 4> kOpaqueBlack{0.0F, 0.0F, 0.0F, 1.0F};
  require(d.clear_value.color == kOpaqueBlack, "opaque black");
  require(d.debug_name == "Main viewport", "named by the viewport");
}

// (viewport.hpp, decide_acquire, after_acquire, presents_at_end_of_frame,
// after_frame_end) and invariants 2 and 3: the first pass of a frame
// acquires and later passes reuse; the frame that acquired presents at its
// end and no other frame does; after the boundary nothing is held and the
// next frame acquires again.
void acquire_once_per_frame_release_at_boundary() {
  const ViewportFrameState idle{kSize, 0};
  require(bw::rhi::decide_acquire(idle, 1) == AcquireDecision::Acquire,
          "the first pass of a frame acquires");
  require(!bw::rhi::presents_at_end_of_frame(idle, 1),
          "a frame that acquired nothing presents nothing");
  const bw::rhi::AcquireResult acquired = bw::rhi::after_acquire(idle, 1, kSize);
  require(acquired.outcome == AcquireOutcome::Held, "a drawable of the back buffer's size is held");
  require(bw::rhi::decide_acquire(acquired.state, 1) == AcquireDecision::Reuse,
          "a later pass of the same frame reuses it");
  require(bw::rhi::presents_at_end_of_frame(acquired.state, 1), "the acquiring frame presents");
  require(!bw::rhi::presents_at_end_of_frame(acquired.state, 2), "another frame does not");
  const ViewportFrameState released = bw::rhi::after_frame_end(acquired.state);
  require(released == idle, "nothing is held after the boundary");
  require(bw::rhi::decide_acquire(released, 2) == AcquireDecision::Acquire,
          "the next frame acquires again");
}

// (viewport.hpp, after_acquire, AcquireOutcome::SizeMismatch) and invariant
// 4: a drawable whose size differs from the back buffer's holds nothing, so
// the frame presents nothing and the next pass would acquire again.
void size_mismatch_holds_nothing() {
  const ViewportFrameState idle{kSize, 0};
  const bw::rhi::AcquireResult mismatch = bw::rhi::after_acquire(idle, 1, kResized);
  require(mismatch.outcome == AcquireOutcome::SizeMismatch, "SizeMismatch");
  require(mismatch.state == idle, "the state is unchanged");
  require(!bw::rhi::presents_at_end_of_frame(mismatch.state, 1), "nothing to present");
  require(bw::rhi::decide_acquire(mismatch.state, 1) == AcquireDecision::Acquire,
          "nothing to reuse");
}

// (viewport.hpp, validate_resize, after_resize) and invariant 5: a zero size
// is ZeroSize, checked first; a resize in the frame that holds the back
// buffer is ResizeDuringFrame; after that frame's boundary the resize is
// accepted, the new size takes effect with nothing held, and only a
// drawable of the new size is held after it.
void resize_refused_while_acquired_then_applies() {
  const ViewportFrameState held =
      bw::rhi::after_acquire(ViewportFrameState{kSize, 0}, 3, kSize).state;
  require(bw::rhi::validate_resize(held, 3, Extent2{0, 1080}) == ViewportError::ZeroSize,
          "ZeroSize before ResizeDuringFrame");
  require(bw::rhi::validate_resize(held, 3, kResized) == ViewportError::ResizeDuringFrame,
          "refused while acquired in the frame being recorded");
  const ViewportFrameState released = bw::rhi::after_frame_end(held);
  require(bw::rhi::validate_resize(released, 4, kResized) == ViewportError::None,
          "accepted after the boundary");
  const ViewportFrameState resized = bw::rhi::after_resize(released, kResized);
  require(resized == ViewportFrameState{kResized, 0}, "the new size, nothing held");
  require(bw::rhi::after_acquire(resized, 4, kSize).outcome == AcquireOutcome::SizeMismatch,
          "a drawable of the old size is a mismatch");
  require(bw::rhi::after_acquire(resized, 4, kResized).outcome == AcquireOutcome::Held,
          "a drawable of the new size is held");
}

// A viewport built from the public type alone: Viewport's constructor is
// protected so a backend derives it, and the test derives it the same way.
// It sets no back buffer; the scenario reads only what the constructor sets.
class TestViewport final : public bw::rhi::Viewport {
public:
  TestViewport(bw::rhi::DeferredDeleteQueue& queue, const ViewportDescription& description) noexcept
      : Viewport(queue, description) {}

private:
  ~TestViewport() override = default;
};

// (viewport.hpp, Viewport constructor): frame_state() starts at the
// description's size with nothing held. (viewport.hpp, description()):
// description().debug_name views the viewport's own copy of the name, not
// the caller's buffer (resources.hpp, DebugName: empty with BW_RHI_DEBUG 0).
void construction_state_and_name() {
  bw::rhi::DeferredDeleteQueue queue;
  int surface = 0;
  std::string caller_name = "Main viewport";
  ViewportDescription d = valid_description(&surface);
  d.debug_name = caller_name;
  const bw::rhi::Ref<TestViewport> viewport(new TestViewport(queue, d));
  require(viewport->frame_state() == ViewportFrameState{kSize, 0},
          "the description's size, nothing held");
  caller_name.assign("Changed name!");
#if BW_RHI_DEBUG
  require(viewport->description().debug_name == "Main viewport",
          "the name given, unchanged by the caller's buffer");
  require(viewport->description().debug_name.data() != caller_name.data(),
          "the name does not view the caller's buffer");
#else
  require(viewport->description().debug_name.empty(), "no name in shipping");
#endif
}

constexpr std::array scenarios{
    Scenario{"validate_viewport_named_in_order", validate_viewport_named_in_order},
    Scenario{"validate_with_backend_kind", validate_with_backend_kind},
    Scenario{"back_buffer_description", back_buffer_description},
    Scenario{"acquire_once_per_frame_release_at_boundary",
             acquire_once_per_frame_release_at_boundary},
    Scenario{"size_mismatch_holds_nothing", size_mismatch_holds_nothing},
    Scenario{"resize_refused_while_acquired_then_applies",
             resize_refused_while_acquired_then_applies},
    Scenario{"construction_state_and_name", construction_state_and_name},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
