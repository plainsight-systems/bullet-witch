#ifndef BW_PLATFORM_MACOS_PAD_HPP
#define BW_PLATFORM_MACOS_PAD_HPP

#include "bw/platform/input/device_input.hpp"
#include "bw/platform/input/pad.hpp"

#include <cstddef>
#include <memory>
#include <span>

// Responsibility: the macOS adapter of bw/platform/input/pad.hpp, over the
// OS's game controller framework. Objective-C++ under the platform
// boundary; nothing above it includes the framework.
//
// Contract.
//   1. create_pad runs on the main thread and returns a pad whether or not
//      a controller is connected; it takes the controllers already
//      connected and then follows the OS's connect and disconnect
//      notifications, which arrive on the main thread (pad.hpp, contract
//      3). It asks the OS for nothing else: a wireless pad is paired in
//      the OS's settings.
//   2. Each poll reads the current values of the controller's extended
//      gamepad profile into an ExtendedGamepadValues and reports the
//      difference from the poll before through pad_samples, the one pure
//      function below; a disconnect is a poll whose current values are all
//      zero.
//   3. pad_samples(previous, current, out) writes one sample for every
//      field of `current` that differs from `previous`, in the order of
//      the table below, and returns the count: an axis field's value as it
//      is, a button field as 1 or 0.
//        a, x, b, y          PadA, PadX, PadB, PadY
//        left_shoulder       PadLeftShoulder
//        right_shoulder      PadRightShoulder
//        left_x, left_y      PadLeftX, PadLeftY
//        right_x, right_y    PadRightX, PadRightY
//        right_trigger       PadRightTrigger
//        left_trigger        PadLeftTrigger
//        left_stick          PadLeftStick
//        right_stick         PadRightStick
//      Precondition: out.size() >= kPadElementCount.
//
// The framework gives an axis value in -1 to 1, dead-zoned and saturated,
// and a button or trigger value in 0 to 1. The stick buttons may be absent
// from the profile; an absent one reads 0. While the application is not
// frontmost the framework forwards no input, its default since macOS 11.3.
namespace bw::platform::macos {

// The extended gamepad profile's current values, as the adapter reads
// them; every field 0 with no controller.
struct ExtendedGamepadValues {
  float left_x{};
  float left_y{};
  float right_x{};
  float right_y{};
  float left_trigger{};
  float right_trigger{};
  bool a{};
  bool b{};
  bool x{};
  bool y{};
  bool left_shoulder{};
  bool right_shoulder{};
  bool left_stick{};
  bool right_stick{};
};

[[nodiscard]] std::size_t pad_samples(const ExtendedGamepadValues& previous_values,
                                      const ExtendedGamepadValues& current_values,
                                      std::span<InputSample> out) noexcept;

[[nodiscard]] std::unique_ptr<Pad> create_pad();

} // namespace bw::platform::macos
#endif
