// Integration test: the macOS window adapter (bw/platform/os/macos/window.hpp)
// through the platform-neutral contract (bw/platform/window/window.hpp). The
// test process is the application: create_window sets it up on the main
// thread, as a host does. A real window opens on this Mac for a few pumps and
// is destroyed at the end of the scenario. No scenario asserts that the
// window becomes key or that a full-screen transition completes: activation
// is at the OS's discretion (window.hpp, Activation) and a process run by
// ctest is not activated.
#include "bw/platform/os/macos/window.hpp"
#include "bw/platform/window/window.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace {

using bw::platform::NativeSurfaceKind;
using bw::platform::PixelSize;
using bw::platform::Window;
using bw::platform::WindowDescription;
using bw::platform::WindowError;
using bw::platform::WindowEvent;
using bw::platform::WindowEventKind;
using bw::platform::WindowMode;

using bw::testing::require;

WindowDescription small_description() {
  WindowDescription description{};
  description.title = "bw window integration";
  description.width = 320;
  description.height = 200;
  return description;
}

// A small window on the main thread; throws unless create_window succeeds.
// The window is small so no screen clamps it.
std::unique_ptr<Window> open_window() {
  WindowError error = WindowError::SystemRefused;
  std::unique_ptr<Window> window = bw::platform::macos::create_window(small_description(), error);
  require(error == WindowError::None && window != nullptr, "create_window succeeds");
  return window;
}

// Pumps and reads every event, `pumps` times, passing each event to `check`.
template <class Check> void pump_and_read(Window& window, int pumps, Check check) {
  for (int i = 0; i < pumps; ++i) {
    window.pump();
    WindowEvent event{};
    while (window.poll_event(event)) {
      check(event);
    }
  }
}

std::string describe(PixelSize size) {
  return std::to_string(size.width) + "x" + std::to_string(size.height);
}

// (macos/window.hpp, create_window): a window is created on the main thread
// with error None. (window.hpp, Window::pixel_size, Sizes): the content area
// in pixels is the description's points times backing_scale(). (window.hpp,
// Window::native_handle; NativeWindowHandle): the handle names a MetalLayer
// surface that is not null.
void create_pump_and_pixel_size() {
  std::unique_ptr<Window> window = open_window();
  pump_and_read(*window, 5, [](const WindowEvent&) {});
  const float scale = window->backing_scale();
  require(scale >= 1.0F, "the backing scale is at least one pixel per point");
  const PixelSize expected{static_cast<std::uint32_t>(std::lround(320.0F * scale)),
                           static_cast<std::uint32_t>(std::lround(200.0F * scale))};
  const PixelSize actual = window->pixel_size();
  require(actual == expected,
          "pixel_size is the description's points times the backing scale: " + describe(actual));
  const bw::platform::NativeWindowHandle handle = window->native_handle();
  require(handle.kind == NativeSurfaceKind::MetalLayer, "the handle's kind is MetalLayer");
  require(handle.surface != nullptr, "the handle's surface is not null");
  window.reset();
}

// (window.hpp, Pump; Window::pump, poll_event): the wiring probe. pump()
// returns, and every event poll_event reads has a kind in WindowEventKind.
// (window.hpp, WindowEvent and the nine constructors): every event read sets
// only its kind's fields, so it equals the constructor of its kind built from
// its own fields. Which events arrive is the OS's (activation, focus and any
// pointer or key the person at the Mac moves are at its discretion), so
// their number is printed, not asserted.
void pumped_events_well_formed() {
  namespace platform = bw::platform;
  std::unique_ptr<Window> window = open_window();
  std::array<int, 9> read{};
  pump_and_read(*window, 10, [&read](const WindowEvent& event) {
    WindowEvent rebuilt{};
    switch (event.kind) {
    case WindowEventKind::Resized:
      rebuilt = platform::resized_event(event.size);
      break;
    case WindowEventKind::CloseRequested:
      rebuilt = platform::close_requested_event();
      break;
    case WindowEventKind::FocusChanged:
      rebuilt = platform::focus_event(event.focused);
      break;
    case WindowEventKind::ModeChanged:
      rebuilt = platform::mode_changed_event(event.mode);
      break;
    case WindowEventKind::ModeRefused:
      rebuilt = platform::mode_refused_event(event.mode);
      break;
    case WindowEventKind::MouseMoved:
      rebuilt = platform::mouse_moved_event(event.position, event.buttons);
      break;
    case WindowEventKind::MouseButton:
      rebuilt = platform::mouse_button_event(event.button, event.pressed, event.position);
      break;
    case WindowEventKind::Scrolled:
      rebuilt = platform::scrolled_event(event.lines);
      break;
    case WindowEventKind::Key:
      rebuilt = platform::key_event(event.key, event.pressed);
      break;
    default:
      require(false, "an event's kind is in WindowEventKind: " +
                         std::to_string(static_cast<unsigned>(event.kind)));
    }
    require(event == rebuilt, "an event of kind " +
                                  std::to_string(static_cast<unsigned>(event.kind)) +
                                  " sets only its kind's fields");
    ++read.at(static_cast<std::size_t>(event.kind));
  });
  // What the OS delivered in this run, for the reader of the test output.
  std::cout << "events read: resized=" << read[0] << " close_requested=" << read[1]
            << " focus_changed=" << read[2] << " mode_changed=" << read[3]
            << " mode_refused=" << read[4] << " mouse_moved=" << read[5]
            << " mouse_button=" << read[6] << " scrolled=" << read[7] << " key=" << read[8] << '\n';
  window.reset();
}

// (window.hpp, Mode): the wiring probe of set_mode. It returns None when the
// window is in the requested mode already, and then nothing is produced: no
// ModeChanged or ModeRefused is read afterwards. The decision's order is the
// unit test of decide_mode_request (unit.window.decisions.mode_request_decision).
void set_mode_to_current_mode_produces_nothing() {
  std::unique_ptr<Window> window = open_window();
  pump_and_read(*window, 5, [](const WindowEvent&) {});
  const WindowMode before = window->mode();
  require(window->set_mode(before) == WindowError::None,
          "set_mode to the current mode returns None");
  pump_and_read(*window, 5, [](const WindowEvent& event) {
    require(event.kind != WindowEventKind::ModeChanged &&
                event.kind != WindowEventKind::ModeRefused,
            "set_mode to the current mode produces no mode event");
  });
  window.reset();
}

// (window.hpp, Preconditions; macos/window.hpp, create_window): create_window
// and set_mode return NotMainThread off the main thread. The other thread
// creates no window and requests no mode change.
void off_main_thread_calls_refused() {
  std::unique_ptr<Window> window = open_window();
  WindowError create_error = WindowError::None;
  bool created = false;
  WindowError mode_error = WindowError::None;
  std::thread other([&window, &create_error, &created, &mode_error] {
    const std::unique_ptr<Window> refused =
        bw::platform::macos::create_window(small_description(), create_error);
    created = refused != nullptr;
    mode_error = window->set_mode(WindowMode::BorderlessFullscreen);
  });
  other.join();
  require(create_error == WindowError::NotMainThread,
          "create_window off the main thread returns NotMainThread");
  require(!created, "create_window off the main thread returns no window");
  require(mode_error == WindowError::NotMainThread,
          "set_mode off the main thread returns NotMainThread");
  window.reset();
}

using Scenario = bw::testing::Scenario<void>;

constexpr std::array kScenarios{
    Scenario{"create_pump_and_pixel_size", &create_pump_and_pixel_size},
    Scenario{"pumped_events_well_formed", &pumped_events_well_formed},
    Scenario{"set_mode_to_current_mode_produces_nothing",
             &set_mode_to_current_mode_produces_nothing},
    Scenario{"off_main_thread_calls_refused", &off_main_thread_calls_refused},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
