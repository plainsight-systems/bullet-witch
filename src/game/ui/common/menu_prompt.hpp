// Original UI prompt glyph runs resolved from the same fixed binding descriptor
// used by action translation. The input owner publishes active family; neither
// device availability nor saved gameplay remaps change UI bindings. Views get
// copied values, no input/Profile authority. Both-phase Core I.1 (#ri-explicit)
// and MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keep
// explicit bounded observations. Source/reference: frontend-menu-repairs.md.
#pragma once
#include <array>
#include <cstdint>

#include "game/player/input/menu_bindings.hpp"

namespace bw::game {
struct MenuPrompt {
  std::array<std::uint16_t, 4> icons{};
  std::uint8_t count{};
  float icon_pitch{}, text_advance{};
};
struct MenuPrompts {
  MenuPrompt confirm, back, navigate;
};
// Resolve physical binding positions to the original admitted icon atlas.
MenuPrompts menu_prompts(MenuInputDevice device) noexcept;
// Reject malformed copied data before publishing any sprite commands.
bool valid_menu_prompt(const MenuPrompt& prompt) noexcept;
}  // namespace bw::game
