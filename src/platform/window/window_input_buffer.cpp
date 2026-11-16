#include "bw/platform/input/device_input.hpp"
#include "bw/platform/window/window.hpp"

#include <cstddef>

// The five invariants of WindowInputBuffer (window.hpp): a fixed ring of
// kWindowInputCapacity key, button and wheel samples and the two motion
// sums, no allocation. pop drains the ring first, then gives each non-zero
// sum once, zeroing it as it goes, so a drained buffer is empty with zero
// sums.
// The wheel is queued per event, never summed: one wheel event is one
// step of the wheel.
namespace bw::platform {

bool WindowInputBuffer::push(const InputSample& sample) noexcept {
  if (count_ == kWindowInputCapacity) {
    return false;
  }
  samples_[(head_ + count_) % kWindowInputCapacity] = sample;
  ++count_;
  return true;
}

void WindowInputBuffer::add_motion(float counts_x, float counts_y) noexcept {
  motion_x_ += counts_x;
  motion_y_ += counts_y;
}

void WindowInputBuffer::clear() noexcept {
  head_ = 0;
  count_ = 0;
  motion_x_ = 0.0F;
  motion_y_ = 0.0F;
}

bool WindowInputBuffer::pop(InputSample& sample) noexcept {
  if (count_ != 0U) {
    sample = samples_[head_];
    head_ = (head_ + 1U) % kWindowInputCapacity;
    --count_;
    return true;
  }
  // The sums, in the order invariant 2 states, each only when not zero.
  const auto take = [&sample](InputElement element, float& sum) {
    if (sum == 0.0F) {
      return false;
    }
    sample = InputSample{element, sum};
    sum = 0.0F;
    return true;
  };
  if (take(InputElement::MouseMotionX, motion_x_) || take(InputElement::MouseMotionY, motion_y_)) {
    return true;
  }
  head_ = 0;
  return false;
}

} // namespace bw::platform
