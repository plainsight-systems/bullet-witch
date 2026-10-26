// Copied logical Help icon runs, resolved by game input policy from an
// immutable settings observation and device availability. Literal artwork is
// not mapped. No Profile/device/GPU authority crosses into presentation. Six
// supported original kico actions, at most four icons each, bounded
// allocation-free work. Core I.1/C.9 (#ri-explicit/#rc-private) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), read for design,
// require explicit observations and no steady-state allocation. Native
// 4ba9a0 -> 4ba3e0 -> 472650/472270, mappings472930/472dd0;
// native-pause-help.md.
#pragma once
#include <array>
#include <cstdint>

#include "game/settings/controller_settings.hpp"
namespace bw::game {
enum class HelpInputAction : std::uint8_t {
  movement,
  camera,
  fire,
  evade,
  attack,
  reload,
  count
};
struct HelpIconRun {
  std::array<std::uint16_t, 4> icons{};
  unsigned count{};
  float pitch{}, advance{};
};
struct HelpInputIcons {
  std::array<HelpIconRun, static_cast<unsigned>(HelpInputAction::count)>
      actions{};
};
HelpInputIcons help_input_icons(const ControllerSettings& settings,
                                bool controller_connected);
bool valid_help_input_icons(const HelpInputIcons& icons) noexcept;
}  // namespace bw::game
