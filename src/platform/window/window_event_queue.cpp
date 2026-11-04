#include "bw/platform/window/window.hpp"

#include <cstddef>

// The four invariants of WindowEventQueue (window.hpp): a fixed ring of
// kWindowEventCapacity events, no allocation.
//
// A CloseRequested pushed while one is already queued since the last clear()
// is dropped and push returns true: the request it carries is already
// recorded, so nothing the host needs is lost. A CloseRequested dropped by a
// full queue is not queued, so a later push of it, once there is room,
// queues it; close_requested() is set either way.
namespace bw::platform {

bool WindowEventQueue::push(const WindowEvent& event) noexcept {
  if (event.kind == WindowEventKind::CloseRequested) {
    close_requested_ = true;
    if (close_queued_) {
      return true;
    }
  }
  if (event.kind == WindowEventKind::Resized && count_ != 0U) {
    WindowEvent& newest = events_[(head_ + count_ - 1U) % kWindowEventCapacity];
    if (newest.kind == WindowEventKind::Resized) {
      newest = event;
      return true;
    }
  }
  if (count_ == kWindowEventCapacity) {
    return false;
  }
  events_[(head_ + count_) % kWindowEventCapacity] = event;
  ++count_;
  if (event.kind == WindowEventKind::CloseRequested) {
    close_queued_ = true;
  }
  return true;
}

bool WindowEventQueue::pop(WindowEvent& event) noexcept {
  if (count_ == 0U) {
    return false;
  }
  event = events_[head_];
  head_ = (head_ + 1U) % kWindowEventCapacity;
  --count_;
  return true;
}

void WindowEventQueue::clear() noexcept {
  head_ = 0;
  count_ = 0;
  close_requested_ = false;
  close_queued_ = false;
}

} // namespace bw::platform
