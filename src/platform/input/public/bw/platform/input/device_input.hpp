#ifndef BW_PLATFORM_INPUT_DEVICE_INPUT_HPP
#define BW_PLATFORM_INPUT_DEVICE_INPUT_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// Responsibility: what a device adapter hands upward (docs/architecture/
// input.md, "Device adapters: keyboard, mouse, pads | src/platform |
// consumed by key state"): one sample of one element of one device. The
// window (bw/platform/window/window.hpp) produces the keyboard's and the
// mouse's, a pad adapter (bw/platform/input/pad.hpp) the pad's, and the
// scripted device (bw/platform/input/scripted_device.hpp) any of them, so
// everything above this type runs the same on all three. Nothing here knows
// an action or a binding; engine/input turns samples into key state.
//
// Contract.
//   1. An element is a button or an axis (element_is_axis). A button's
//      sample is 1 when it went down and 0 when it came up. A stick axis's
//      sample is its position in -1 to 1, right and up positive. A
//      trigger's is its travel in 0 to 1. MouseMotionX and MouseMotionY
//      are relative motion in device counts, right and down positive.
//      MouseWheel is the wheel's travel in lines with the OS's sign, after
//      the user's scroll-direction preference, as the window's Scrolled
//      event reports it.
//   2. element_name and element_from_name are inverses over every element;
//      a name not in the table gives no element. The names are the
//      enumerators' own spellings.
//
// The elements are the ones a binding of the game names (the game's
// binding table); a device has more, and each is added with the binding
// that names it.
//
// Core guidelines read: Enum.2 and Enum.3 (one class enum for the element
// set), I.1 (a sample is every input of what it means), Con.5 (the name
// table is constexpr). Performance guidelines read: COPY.4 (a sample is a
// plain value of eight bytes), EMB.2 (fixed tables, no allocation).
namespace bw::platform {

enum class InputElement : std::uint8_t {
  // Keyboard, by position on a US layout (window.hpp, Input).
  KeyW,
  KeyA,
  KeyS,
  KeyD,
  KeyE,
  KeyR,
  KeyZ,
  KeyX,
  KeyC,
  KeySpace,
  KeyLeftShift,
  // Mouse.
  MouseLeft,
  MouseRight,
  MouseMiddle,
  MouseMotionX,
  MouseMotionY,
  MouseWheel,
  // Pad.
  PadLeftX,
  PadLeftY,
  PadRightX,
  PadRightY,
  PadLeftTrigger,
  PadRightTrigger,
  PadA,
  PadB,
  PadX,
  PadY,
  PadLeftShoulder,
  PadRightShoulder,
  PadLeftStick,
  PadRightStick,
};

inline constexpr std::size_t kInputElementCount = 31;

struct InputSample {
  InputElement element{InputElement::KeyW};
  float value{};
  [[nodiscard]] friend constexpr bool operator==(const InputSample&,
                                                 const InputSample&) noexcept = default;
};

[[nodiscard]] constexpr bool element_is_axis(InputElement element) noexcept {
  switch (element) {
  case InputElement::MouseMotionX:
  case InputElement::MouseMotionY:
  case InputElement::MouseWheel:
  case InputElement::PadLeftX:
  case InputElement::PadLeftY:
  case InputElement::PadRightX:
  case InputElement::PadRightY:
  case InputElement::PadLeftTrigger:
  case InputElement::PadRightTrigger:
    return true;
  default:
    return false;
  }
}

// The enumerators' spellings, in enumerator order.
inline constexpr std::array<std::string_view, kInputElementCount> kInputElementNames{
    "KeyW",
    "KeyA",
    "KeyS",
    "KeyD",
    "KeyE",
    "KeyR",
    "KeyZ",
    "KeyX",
    "KeyC",
    "KeySpace",
    "KeyLeftShift",
    "MouseLeft",
    "MouseRight",
    "MouseMiddle",
    "MouseMotionX",
    "MouseMotionY",
    "MouseWheel",
    "PadLeftX",
    "PadLeftY",
    "PadRightX",
    "PadRightY",
    "PadLeftTrigger",
    "PadRightTrigger",
    "PadA",
    "PadB",
    "PadX",
    "PadY",
    "PadLeftShoulder",
    "PadRightShoulder",
    "PadLeftStick",
    "PadRightStick",
};

[[nodiscard]] constexpr std::string_view element_name(InputElement element) noexcept {
  return kInputElementNames[static_cast<std::size_t>(element)];
}

[[nodiscard]] constexpr std::optional<InputElement>
element_from_name(std::string_view name) noexcept {
  for (std::size_t index = 0; index < kInputElementCount; ++index) {
    if (kInputElementNames[index] == name) {
      return static_cast<InputElement>(index);
    }
  }
  return std::nullopt;
}

} // namespace bw::platform
#endif
