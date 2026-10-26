// Native PC fixed UI-mode prompts, copied into presentation snapshots. Device
// availability selects keyboard or admitted Xbox artwork; gameplay remapping
// does not alter these UI defaults. Views receive no input or Profile
// authority. Source 472270/472930/472dd0; evidence:
// native-pause/menu-prompts.md. Core I.1
// (#ri-explicit) and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md), read for design and implementation,
// require explicit values and bounded, allocation-free publication.
#pragma once
#include <array>
#include <cstdint>

namespace bw::game {
struct MenuPrompt {
  std::array<std::uint16_t, 4> icons{};
  std::uint8_t count{};
  float icon_pitch{}, text_advance{};
};
struct MenuPrompts {
  MenuPrompt confirm, back;
};
// No last-used-device heuristic: original device discovery/loss selects this
// mode. The platform has already resolved device availability in its sample.
MenuPrompts menu_prompts(bool controller_connected) noexcept;
// Reject malformed copied data before publishing any sprite commands.
bool valid_menu_prompt(const MenuPrompt& prompt) noexcept;
}  // namespace bw::game
