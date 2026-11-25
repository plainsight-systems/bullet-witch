#ifndef BW_INPUT_KEYS_HPP
#define BW_INPUT_KEYS_HPP

#include "bw/platform/input/device_input.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

// Responsibility: the keys a mapping names (docs/architecture/input.md,
// Key: "one input of a device: a button, an axis, or a pair of axes read
// as one"). A key is what a context binds; a device element
// (bw/platform/input/device_input.hpp) is what an adapter reports. The two
// differ where input.md says so: a stick and the mouse's motion are two
// one-axis keys and one paired two-axis key, and the wheel is two button
// keys.
//
// Contract.
//   1. key_kind(key) is Button, Axis1D or Axis2D. The Axis2D keys are
//      Mouse2D, PadLeft2D and PadRight2D; pair_axes(key) names the two
//      one-axis keys an Axis2D key reads as its x and y, and is empty for
//      every other key.
//   2. key_for_element(element) is the key a sample of that element
//      feeds: the key of the same name, MouseMotionX and MouseMotionY the
//      keys MouseX and MouseY. MouseWheel has no single key
//      (key_state.hpp, invariant 3) and gives none.
//   3. reads_zero_without_samples(key) is true for MouseX, MouseY and
//      Mouse2D and false for every other key (input.md, invariant 2).
//   4. key_name and key_from_name are inverses over every key; a name not
//      in the table gives no key. The names are the enumerators' own
//      spellings.
namespace bw::input {

enum class Key : std::uint8_t {
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
  MouseLeft,
  MouseRight,
  MouseMiddle,
  MouseX,
  MouseY,
  Mouse2D,
  MouseScrollUp,
  MouseScrollDown,
  PadLeftX,
  PadLeftY,
  PadLeft2D,
  PadRightX,
  PadRightY,
  PadRight2D,
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
inline constexpr std::size_t kKeyCount = 35;

enum class KeyKind : std::uint8_t { Button, Axis1D, Axis2D };

struct KeyPair {
  Key x{Key::KeyW};
  Key y{Key::KeyW};
};

[[nodiscard]] KeyKind key_kind(Key key) noexcept;
[[nodiscard]] std::optional<KeyPair> pair_axes(Key key) noexcept;
[[nodiscard]] std::optional<Key> key_for_element(platform::InputElement element) noexcept;
[[nodiscard]] bool reads_zero_without_samples(Key key) noexcept;
[[nodiscard]] std::string_view key_name(Key key) noexcept;
[[nodiscard]] std::optional<Key> key_from_name(std::string_view name) noexcept;

} // namespace bw::input
#endif
