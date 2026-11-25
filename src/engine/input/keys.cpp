// The key set (bw/input/keys.hpp): kinds, pairs, the element each key is
// fed by, and the name table.
#include "bw/input/keys.hpp"

#include "bw/platform/input/device_input.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace bw::input {
namespace {

// The enumerators' spellings, in enumerator order (contract 4).
constexpr std::array<std::string_view, kKeyCount> kKeyNames{
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
    "MouseX",
    "MouseY",
    "Mouse2D",
    "MouseScrollUp",
    "MouseScrollDown",
    "PadLeftX",
    "PadLeftY",
    "PadLeft2D",
    "PadRightX",
    "PadRightY",
    "PadRight2D",
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

} // namespace

KeyKind key_kind(Key key) noexcept {
  switch (key) {
  case Key::MouseX:
  case Key::MouseY:
  case Key::PadLeftX:
  case Key::PadLeftY:
  case Key::PadRightX:
  case Key::PadRightY:
  case Key::PadLeftTrigger:
  case Key::PadRightTrigger:
    return KeyKind::Axis1D;
  case Key::Mouse2D:
  case Key::PadLeft2D:
  case Key::PadRight2D:
    return KeyKind::Axis2D;
  default:
    return KeyKind::Button;
  }
}

std::optional<KeyPair> pair_axes(Key key) noexcept {
  switch (key) {
  case Key::Mouse2D:
    return KeyPair{Key::MouseX, Key::MouseY};
  case Key::PadLeft2D:
    return KeyPair{Key::PadLeftX, Key::PadLeftY};
  case Key::PadRight2D:
    return KeyPair{Key::PadRightX, Key::PadRightY};
  default:
    return std::nullopt;
  }
}

// An element feeds the key of the same name, the mouse's motion the keys
// MouseX and MouseY; the wheel feeds its two button keys through key state
// (key_state.hpp, invariant 3), so it has no single key here.
std::optional<Key> key_for_element(platform::InputElement element) noexcept {
  switch (element) {
  case platform::InputElement::MouseMotionX:
    return Key::MouseX;
  case platform::InputElement::MouseMotionY:
    return Key::MouseY;
  case platform::InputElement::MouseWheel:
    return std::nullopt;
  default:
    return key_from_name(platform::element_name(element));
  }
}

// Mouse motion is reported as movement since the last sample, so a frame with no motion reads 0.
bool reads_zero_without_samples(Key key) noexcept {
  return key == Key::MouseX || key == Key::MouseY || key == Key::Mouse2D;
}

std::string_view key_name(Key key) noexcept { return kKeyNames[static_cast<std::size_t>(key)]; }

std::optional<Key> key_from_name(std::string_view name) noexcept {
  for (std::size_t index = 0; index < kKeyCount; ++index) {
    if (kKeyNames[index] == name) {
      return static_cast<Key>(index);
    }
  }
  return std::nullopt;
}

} // namespace bw::input
