// Fixed original UI mode-two bindings, shared by action translation and glyph
// resolution. Saved gameplay/Help mappings remain a different context. Keys
// are source physical codes; pad values are neutral positions. Views receive
// copied glyph runs, never this owner's mutable state or a Profile reference.
// Active device is input-owner history, not connection availability. Original
// source and the pinned CommonInput reference are recorded in
// frontend-menu-repairs.md. Both-phase Core I.1 (#ri-explicit) and performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) use explicit
// immutable bindings and bounded allocation-free observations.
#pragma once
#include <array>
#include <cstdint>

#include "bw/input/physical_state.hpp"
namespace bw::game {
enum class MenuInputDevice : std::uint8_t { keyboard_mouse, controller };
struct MenuBindings {
  std::array<std::uint16_t, 2> confirm_keys, back_keys;
  // Forward/back/left/right, matching source UI mode-two ordering.
  std::array<std::uint16_t, 4> navigation_keys;
  std::array<std::uint16_t, 2> wheel_keys;
  input::PadButton confirm_button, back_button;
};
inline constexpr MenuBindings kOriginalMenuBindings{
    {0x12c, 0x39},  {0x12d, 0x2a},           {0x11, 0x1f, 0x1e, 0x20},
    {0x134, 0x135}, input::PadButton::south, input::PadButton::east};
}  // namespace bw::game
