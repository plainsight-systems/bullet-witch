// Integration test: the Metal viewport against its surface. The acquisition
// failure reaches the caller through Device::execute (viewport.hpp,
// invariant 4; command_list.hpp, ExecuteError), and vsync is applied to the
// surface (viewport.hpp, invariant 6). Each scenario opens a real window on
// this Mac (macos/window.hpp, create_window) in this process, which is the
// application, creates a viewport from its native handle and drives the
// device through the public headers only. It is Objective-C++ because the
// surface is the one place to put a change behind the viewport's back or to
// read what the viewport applied: window.hpp states that a MetalLayer
// handle's surface is a CAMetalLayer, and the scenarios set that layer's
// drawable size or read its display sync.
//
// The pure bookkeeping behind the failure (viewport.hpp, after_acquire,
// AcquireOutcome::SizeMismatch) is unit tested; the first scenario is the
// probe that the Metal path is wired, the second that a pass refused at its
// second back buffer holds neither, the third that vsync reaches the
// layer. BackBufferUnavailable is not probed here: no
// public interface can exhaust the layer's drawables, so that failure stays
// covered by the pure functions and the replay's unit test only.
//
// Arguments: the scenario name.
#import <QuartzCore/CAMetalLayer.h>

#include "bw/platform/os/macos/window.hpp"
#include "bw/platform/window/window.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/build.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"
#include "bw/rendering/rhi/transitions.hpp"
#include "bw/rendering/rhi/viewport.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstdlib>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace bw::rhi;

using bw::testing::require;

template <class T, class E> T take(Result<T, E> result, std::string_view what) {
  if (!result.ok()) {
    throw std::runtime_error(std::string(what) + " failed with code " +
                             std::to_string(static_cast<int>(result.error())) + ": " +
                             std::string(result.detail()));
  }
  return std::move(result.value());
}

#if BW_RHI_DEBUG
// Validation failures captured instead of aborting, with the names copied
// (the reporter's views live for the call only).
struct CapturedFailure {
  ValidationCode code{ValidationCode::None};
  std::string resource;
  std::string command;
};
void capture_failure(const ValidationFailure& failure, void* user) noexcept {
  auto* failures = static_cast<std::vector<CapturedFailure>*>(user);
  try {
    failures->push_back(CapturedFailure{failure.code, std::string(failure.resource_name),
                                        std::string(failure.command)});
  } catch (...) {
    std::abort();
  }
}
#else
// No validation layer exists in this configuration (build.hpp).
struct CapturedFailure {};
#endif

std::unique_ptr<Device> make_device(std::vector<CapturedFailure>& failures) {
  DeviceDescription description{};
  description.backend = BackendKind::Metal;
#if BW_RHI_DEBUG
  description.reporter = &capture_failure;
  description.reporter_user = &failures;
#else
  static_cast<void>(failures);
#endif
  return take(Device::create(description), "Device::create");
}

void require_no_validation_failure(const std::vector<CapturedFailure>& failures) {
#if BW_RHI_DEBUG
  if (!failures.empty()) {
    throw std::runtime_error("validation failure " +
                             std::to_string(static_cast<int>(failures.front().code)) + " on " +
                             failures.front().resource + " in " + failures.front().command);
  }
#else
  static_cast<void>(failures);
#endif
}

// A small window, so no screen clamps its pixel size, pumped until AppKit
// has shown it; the events are read and dropped.
std::unique_ptr<bw::platform::Window> open_window(std::string_view title) {
  bw::platform::WindowDescription description{};
  description.title = title;
  description.width = 320;
  description.height = 200;
  bw::platform::WindowError error = bw::platform::WindowError::SystemRefused;
  std::unique_ptr<bw::platform::Window> window =
      bw::platform::macos::create_window(description, error);
  require(error == bw::platform::WindowError::None && window != nullptr, "create_window succeeds");
  for (int i = 0; i < 3; ++i) {
    window->pump();
    bw::platform::WindowEvent event{};
    while (window->poll_event(event)) {
    }
  }
  return window;
}

Ref<Viewport> window_viewport(Device& device, const bw::platform::Window& window,
                              std::string_view name) {
  ViewportDescription description{};
  description.window = window.native_handle();
  description.pixel_size = Extent2{window.pixel_size().width, window.pixel_size().height};
  description.debug_name = name;
  return take(device.create_viewport(description), "create_viewport");
}

// The viewport's CAMetalLayer (window.hpp, NativeWindowHandle).
CAMetalLayer* layer_of(const Viewport& viewport) {
  return (__bridge CAMetalLayer*)viewport.description().window.surface;
}

// Records one frame: the transition of every back buffer from Present, one
// clear pass named `pass_name` on all of them, the transition back to
// Present. An execute failure's detail views the list, so the caller keeps
// it.
void record_clear_frame(CommandList& list, std::span<Texture* const> back_buffers,
                        std::string_view pass_name) {
  std::vector<Transition> to_target;
  std::vector<Transition> to_present;
  to_target.reserve(back_buffers.size());
  to_present.reserve(back_buffers.size());
  RenderPassDescription pass{};
  for (Texture* const back_buffer : back_buffers) {
    to_target.push_back(Transition{back_buffer, Access::Present, Access::RenderTarget});
    to_present.push_back(Transition{back_buffer, Access::RenderTarget, Access::Present});
    pass.colors[pass.color_count++] =
        ColorTarget{back_buffer, 0, 0, LoadAction::Clear, StoreAction::Store};
  }
  pass.name = pass_name;
  list.transition(TransitionBatch{std::span<const Transition>{to_target}});
  list.begin_render_pass(pass);
  list.end_render_pass();
  list.transition(TransitionBatch{std::span<const Transition>{to_present}});
}

void require_ok(const Status<ExecuteError>& executed, std::string_view what) {
  require(executed.ok(), std::string(what) + ": code " +
                             std::to_string(static_cast<int>(executed.error())) + " " +
                             std::string(executed.detail()));
}

// (viewport.hpp, invariant 4) and (command_list.hpp,
// ExecuteError::BackBufferSizeMismatch): when the surface's drawable size
// differs from the viewport's size, the first pass that binds the back
// buffer fails, Device::execute returns BackBufferSizeMismatch with the
// pass's name as the detail, and the frame is not drawn; (device.hpp,
// resize_viewport; viewport.hpp, invariant 5) once the host resizes the
// viewport to the drawable's size, the next frame executes. The layer's
// size is changed before any frame is presented, so no drawable of the old
// size is pooled in the layer.
void stale_drawable_size_is_named() {
  const std::unique_ptr<bw::platform::Window> window = open_window("stale drawable size");
  std::vector<CapturedFailure> failures;
  const std::unique_ptr<Device> device = make_device(failures);
  const Ref<Viewport> viewport = window_viewport(*device, *window, "stale viewport");
  const Extent2 stale{viewport->size().width / 2U, viewport->size().height / 2U};
  require(stale.width > 0U && stale.height > 0U, "the window is large enough to halve");
  layer_of(*viewport).drawableSize =
      CGSizeMake(static_cast<CGFloat>(stale.width), static_cast<CGFloat>(stale.height));

  constexpr std::string_view kStalePass = "stale clear";
  CommandList stale_list;
  record_clear_frame(stale_list, std::array{&viewport->back_buffer()}, kStalePass);
  const Status<ExecuteError> refused = device->execute(stale_list);
  require(refused.error() == ExecuteError::BackBufferSizeMismatch,
          "a drawable of another size is BackBufferSizeMismatch: code " +
              std::to_string(static_cast<int>(refused.error())));
  require(refused.detail() == kStalePass, "the failure names the pass");
  static_cast<void>(device->end_frame());

  require(device->resize_viewport(*viewport, stale).ok(),
          "resize_viewport to the drawable's size succeeds");
  CommandList resized_list;
  record_clear_frame(resized_list, std::array{&viewport->back_buffer()}, "resized clear");
  require_ok(device->execute(resized_list), "the frame after the resize executes");
  static_cast<void>(device->end_frame());
  require_no_validation_failure(failures);
}

// (viewport.hpp, invariant 4: the replay stops at the failed pass and the
// frame is not drawn, and a pass that fails to acquire any of its back
// buffers holds none of the back buffers it acquired; invariant 5:
// ResizeDuringFrame only while the back
// buffer is acquired in the frame being recorded): a pass whose first back
// buffer is acquired and whose second is refused (its layer resized behind
// its viewport) leaves neither held, so resizing the first viewport in the
// same frame succeeds; (device.hpp, EndFrameResult::gpu_fault) the frame then
// ends with no fault, and after both viewports are resized the same pass
// executes. The windows share one description, so their back buffers have
// one extent, as a pass requires (pipeline_states.hpp, ExtentMismatch).
void failed_pass_holds_no_back_buffer() {
  const std::unique_ptr<bw::platform::Window> first_window = open_window("held back buffer");
  const std::unique_ptr<bw::platform::Window> second_window = open_window("refused back buffer");
  std::vector<CapturedFailure> failures;
  const std::unique_ptr<Device> device = make_device(failures);
  const Ref<Viewport> first = window_viewport(*device, *first_window, "first viewport");
  const Ref<Viewport> second = window_viewport(*device, *second_window, "second viewport");
  require(first->size() == second->size(), "the two windows have one pixel size");
  const Extent2 stale{first->size().width / 2U, first->size().height / 2U};
  require(stale.width > 0U && stale.height > 0U, "the windows are large enough to halve");
  layer_of(*second).drawableSize =
      CGSizeMake(static_cast<CGFloat>(stale.width), static_cast<CGFloat>(stale.height));

  constexpr std::string_view kRefusedPass = "two back buffers";
  CommandList refused_list;
  record_clear_frame(refused_list, std::array{&first->back_buffer(), &second->back_buffer()},
                     kRefusedPass);
  const Status<ExecuteError> refused = device->execute(refused_list);
  require(refused.error() == ExecuteError::BackBufferSizeMismatch,
          "the second back buffer is BackBufferSizeMismatch: code " +
              std::to_string(static_cast<int>(refused.error())));
  require(refused.detail() == kRefusedPass, "the failure names the pass");
  const Status<ViewportError> first_resize = device->resize_viewport(*first, stale);
  require(first_resize.ok(), "the first viewport is not held after the failed pass: code " +
                                 std::to_string(static_cast<int>(first_resize.error())));
  require(device->resize_viewport(*second, stale).ok(), "the second viewport resizes");
  require(!device->end_frame().gpu_fault, "the frame of the failed pass ends with no fault");

  CommandList resized_list;
  record_clear_frame(resized_list, std::array{&first->back_buffer(), &second->back_buffer()},
                     "resized pass");
  require_ok(device->execute(resized_list), "the pass executes after both resizes");
  require(!device->end_frame().gpu_fault, "the presented frame ends with no fault");
  require_no_validation_failure(failures);
}

// (viewport.hpp, invariant 6: vsync is a description field applied to the
// surface and reported back by vsync(); set_vsync changes it between frames
// and reports the new value; device.hpp, set_vsync): the layer's
// displaySyncEnabled follows the description's default (on), then
// set_vsync(false), then set_vsync(true), and vsync() reports each value; a
// frame presents after each change.
void viewport_vsync_reported() {
  const std::unique_ptr<bw::platform::Window> window = open_window("viewport vsync");
  std::vector<CapturedFailure> failures;
  const std::unique_ptr<Device> device = make_device(failures);
  const Ref<Viewport> viewport = window_viewport(*device, *window, "vsync viewport");
  CAMetalLayer* const layer = layer_of(*viewport);
  const auto present_frame = [&device, &viewport](std::string_view pass_name) {
    CommandList list;
    record_clear_frame(list, std::array{&viewport->back_buffer()}, pass_name);
    require_ok(device->execute(list), "the frame executes");
    require(!device->end_frame().gpu_fault, "the frame ends with no fault");
  };

  require(viewport->vsync(), "vsync is the description's default, on");
  require(layer.displaySyncEnabled, "the layer's display sync is on at creation");

  device->set_vsync(*viewport, false);
  require(!viewport->vsync(), "set_vsync(false) is reported");
  require(!layer.displaySyncEnabled, "set_vsync(false) turns the layer's display sync off");
  present_frame("vsync off");

  device->set_vsync(*viewport, true);
  require(viewport->vsync(), "set_vsync(true) is reported");
  require(layer.displaySyncEnabled, "set_vsync(true) turns the layer's display sync on");
  present_frame("vsync on");
  require_no_validation_failure(failures);
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{
    Scenario{"stale_drawable_size_is_named", stale_drawable_size_is_named},
    Scenario{"failed_pass_holds_no_back_buffer", failed_pass_holds_no_back_buffer},
    Scenario{"viewport_vsync_reported", viewport_vsync_reported},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
