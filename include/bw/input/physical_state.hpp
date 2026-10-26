// Neutral device sample: USB keyboard usages, positional controller buttons,
// mouse buttons/wheel/motion, and normalized sticks/triggers. No game mapping
// or saved-profile policy lives here. Platform publishes copied held/pressed
// observations once per pump; focus loss publishes zero state. Presses survive
// down/up events consumed in one pump. Fixed storage and no ownership transfer.
// Core I.1 (#ri-explicit), E.4 (#re-design-invariants), performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), read in design and
// implementation, keep sampling explicit and allocation-free.
#pragma once
#include <array>
#include <bitset>
#include <cstdint>
namespace bw::input {
inline constexpr std::size_t kKeyboardUsageCount = 256;
enum class PadButton : std::uint8_t {
  up,
  down,
  left,
  right,
  menu,
  view,
  left_stick,
  right_stick,
  left_shoulder,
  right_shoulder,
  left_trigger,
  right_trigger,
  south,
  east,
  west,
  north
};
constexpr std::uint16_t pad_bit(PadButton value) {
  return static_cast<std::uint16_t>(1U << static_cast<unsigned>(value));
}
struct PhysicalState {
  std::bitset<kKeyboardUsageCount> keys_held, keys_pressed;
  std::uint16_t pad_held{}, pad_pressed{};
  std::uint8_t mouse_held{}, mouse_pressed{};
  std::array<float, 2> movement{}, look{}, mouse_delta{};
  float left_trigger{}, right_trigger{};
  int wheel{};
  bool controller_connected{};
};
}  // namespace bw::input
