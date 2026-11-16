// The difference between two readings of the extended gamepad profile as
// device samples (bw/platform/os/macos/pad.hpp, contract 3). Pure C++ with
// no OS include, in a library of its own, so its unit test links no
// framework; the adapter (pad.mm) reads the profile and calls it.
#include "bw/platform/input/device_input.hpp"
#include "bw/platform/input/pad.hpp"
#include "bw/platform/os/macos/pad.hpp"

#include <cstddef>
#include <span>

namespace bw::platform::macos {

// One sample per changed field, in the header's table order: the face
// and shoulder buttons, the four stick axes, the right then the left
// trigger, then the stick buttons. An axis passes through as the OS gave
// it, with no dead zone, scale or clamp (pad.hpp, contract 5); a button is
// 1 or 0.
std::size_t pad_samples(const ExtendedGamepadValues& previous_values,
                        const ExtendedGamepadValues& current_values,
                        std::span<InputSample> out) noexcept {
  std::size_t count = 0;
  const auto axis = [&](float previous, float current, InputElement element) {
    if (previous != current) {
      out[count] = InputSample{element, current};
      ++count;
    }
  };
  const auto button = [&](bool previous, bool current, InputElement element) {
    if (previous != current) {
      out[count] = InputSample{element, current ? 1.0F : 0.0F};
      ++count;
    }
  };
  button(previous_values.a, current_values.a, InputElement::PadA);
  button(previous_values.x, current_values.x, InputElement::PadX);
  button(previous_values.b, current_values.b, InputElement::PadB);
  button(previous_values.y, current_values.y, InputElement::PadY);
  button(previous_values.left_shoulder, current_values.left_shoulder,
         InputElement::PadLeftShoulder);
  button(previous_values.right_shoulder, current_values.right_shoulder,
         InputElement::PadRightShoulder);
  axis(previous_values.left_x, current_values.left_x, InputElement::PadLeftX);
  axis(previous_values.left_y, current_values.left_y, InputElement::PadLeftY);
  axis(previous_values.right_x, current_values.right_x, InputElement::PadRightX);
  axis(previous_values.right_y, current_values.right_y, InputElement::PadRightY);
  axis(previous_values.right_trigger, current_values.right_trigger, InputElement::PadRightTrigger);
  axis(previous_values.left_trigger, current_values.left_trigger, InputElement::PadLeftTrigger);
  button(previous_values.left_stick, current_values.left_stick, InputElement::PadLeftStick);
  button(previous_values.right_stick, current_values.right_stick, InputElement::PadRightStick);
  return count;
}

} // namespace bw::platform::macos
