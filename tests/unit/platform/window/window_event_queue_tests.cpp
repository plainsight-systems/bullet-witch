// Unit test: the invariants of WindowEventQueue (bw/platform/window/window.hpp),
// one scenario per CTest entry. Each scenario names the invariant it asserts.
#include "bw/platform/window/window.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

using bw::platform::kWindowEventCapacity;
using bw::platform::MouseButton;
using bw::platform::PixelPoint;
using bw::platform::PixelSize;
using bw::platform::WindowEvent;
using bw::platform::WindowEventKind;
using bw::platform::WindowEventQueue;
using bw::platform::WindowMode;

using bw::testing::require;

WindowEvent resized(std::uint32_t width, std::uint32_t height) {
  return WindowEvent{WindowEventKind::Resized, PixelSize{width, height}, WindowMode::Windowed,
                     false};
}
WindowEvent close_requested() {
  return WindowEvent{WindowEventKind::CloseRequested, {}, WindowMode::Windowed, false};
}
WindowEvent focus(bool focused) {
  return WindowEvent{WindowEventKind::FocusChanged, {}, WindowMode::Windowed, focused};
}
WindowEvent mode_changed(WindowMode mode) {
  return WindowEvent{WindowEventKind::ModeChanged, {}, mode, false};
}

// (window.hpp, WindowEventQueue invariant 1): events pop in push order.
void pop_in_push_order() {
  WindowEventQueue queue;
  const std::array pushed{focus(true), resized(640, 400),
                          mode_changed(WindowMode::BorderlessFullscreen), focus(false),
                          close_requested()};
  for (const WindowEvent& event : pushed) {
    require(queue.push(event), "an event pushed into a queue with room is accepted");
  }
  require(queue.size() == pushed.size(), "every pushed event is queued");
  for (const WindowEvent& expected : pushed) {
    WindowEvent popped{};
    require(queue.pop(popped), "a queued event pops");
    require(popped == expected, "events pop in push order");
  }
  WindowEvent none{};
  require(!queue.pop(none) && queue.empty(), "an empty queue pops nothing");
}

// (window.hpp, WindowEventQueue invariant 2): a Resized pushed while the
// newest queued event is a Resized replaces it; a Resized after another kind
// is queued.
void consecutive_resizes_coalesce() {
  WindowEventQueue queue;
  require(queue.push(resized(100, 100)), "the first resize is accepted");
  require(queue.push(resized(200, 150)), "a second resize is accepted");
  require(queue.push(resized(300, 200)), "a third resize is accepted");
  require(queue.size() == 1U, "consecutive resizes coalesce to one event");
  require(queue.push(focus(true)), "a focus event is accepted");
  require(queue.push(resized(400, 300)), "a resize after another kind is accepted");
  require(queue.size() == 3U, "a resize after another kind is not coalesced");
  WindowEvent popped{};
  require(queue.pop(popped) && popped == resized(300, 200),
          "the coalesced resize is the last size");
  require(queue.pop(popped) && popped == focus(true), "the focus event follows");
  require(queue.pop(popped) && popped == resized(400, 300), "the later resize is its own event");
}

// (window.hpp, WindowEventQueue invariant 3): the first CloseRequested is
// queued and sets close_requested(); later pushes of it are dropped;
// close_requested() stays true until clear().
void close_is_sticky() {
  WindowEventQueue queue;
  require(!queue.close_requested(), "a new queue has no close request");
  require(queue.push(close_requested()), "the first close is accepted");
  require(queue.close_requested(), "the first close sets close_requested");
  static_cast<void>(queue.push(close_requested()));
  require(queue.size() == 1U, "a later close is dropped");
  WindowEvent popped{};
  require(queue.pop(popped) && popped.kind == WindowEventKind::CloseRequested,
          "the queued close pops");
  require(queue.close_requested(), "close_requested stays true after the event pops");
  static_cast<void>(queue.push(close_requested()));
  require(queue.empty(), "a close pushed after the first popped is still dropped");
  require(queue.close_requested(), "close_requested stays true");
  queue.clear();
  require(!queue.close_requested() && queue.empty(), "clear resets the close request");
  require(queue.push(close_requested()) && queue.size() == 1U,
          "after clear a close is queued again");
}

// (window.hpp, WindowEventQueue invariant 4): a full queue drops the pushed
// event and push returns false; a dropped CloseRequested still sets
// close_requested().
void full_queue_drops_and_reports() {
  WindowEventQueue queue;
  for (std::size_t i = 0; i < kWindowEventCapacity; ++i) {
    require(queue.push(focus(i % 2U == 0U)), "a queue with room accepts the event");
  }
  require(queue.size() == kWindowEventCapacity, "the queue holds its capacity");
  require(!queue.push(mode_changed(WindowMode::Windowed)), "a full queue refuses the event");
  require(!queue.push(resized(10, 10)), "a full queue refuses a resize after another kind");
  require(queue.size() == kWindowEventCapacity, "the refused events are dropped");
  require(!queue.push(close_requested()), "a full queue refuses a close");
  require(queue.close_requested(), "a dropped close still sets close_requested");
  WindowEvent popped{};
  for (std::size_t i = 0; i < kWindowEventCapacity; ++i) {
    require(queue.pop(popped) && popped == focus(i % 2U == 0U),
            "the accepted events pop in push order");
  }
  require(queue.empty(), "only the accepted events were queued");
}

// (window.hpp, WindowEventQueue invariant 5): a MouseMoved pushed while the
// newest queued event is a MouseMoved replaces it; a button event between two
// moves keeps both moves; a Scrolled never coalesces.
void mouse_moves_coalesce() {
  using bw::platform::mouse_button_event;
  using bw::platform::mouse_moved_event;
  using bw::platform::scrolled_event;
  const std::uint8_t left = bw::platform::mouse_button_bit(MouseButton::Left);
  WindowEventQueue queue;
  require(queue.push(mouse_moved_event(PixelPoint{1, 2}, 0)), "the first move is accepted");
  require(queue.push(mouse_moved_event(PixelPoint{3, 4}, 0)), "a second move is accepted");
  require(queue.push(mouse_moved_event(PixelPoint{-5, 6}, left)), "a third move is accepted");
  require(queue.size() == 1U, "consecutive moves coalesce to one event");
  WindowEvent popped{};
  require(queue.pop(popped) && popped == mouse_moved_event(PixelPoint{-5, 6}, left),
          "the coalesced move is the last position and buttons");

  const std::array kept{mouse_moved_event(PixelPoint{10, 10}, 0),
                        mouse_button_event(MouseButton::Left, true, PixelPoint{10, 10}),
                        mouse_moved_event(PixelPoint{12, 11}, left), scrolled_event(1.0F),
                        scrolled_event(-2.0F)};
  for (const WindowEvent& event : kept) {
    require(queue.push(event), "an event pushed into a queue with room is accepted");
  }
  require(queue.size() == kept.size(),
          "a button between two moves keeps both moves and two scrolls stay two");
  for (const WindowEvent& expected : kept) {
    require(queue.pop(popped) && popped == expected, "the events pop as pushed");
  }
}

using Scenario = bw::testing::Scenario<void>;

constexpr std::array kScenarios{
    Scenario{"pop_in_push_order", &pop_in_push_order},
    Scenario{"consecutive_resizes_coalesce", &consecutive_resizes_coalesce},
    Scenario{"close_is_sticky", &close_is_sticky},
    Scenario{"full_queue_drops_and_reports", &full_queue_drops_and_reports},
    Scenario{"mouse_moves_coalesce", &mouse_moves_coalesce},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
