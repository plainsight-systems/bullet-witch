// Copied Bullet Witch input intentions, produced by ControllerInput from one
// neutral device sample and authoritative settings. UI and mission consumers
// borrow no device, profile or repeat owner; this value carries no mutation
// capability. Fixed UI actions remain separate from saved gameplay bindings.
// No allocation, native handles or clock reads. Both-phase Core I.1
// (#ri-explicit) separates observations from policy; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps snapshots
// inline.
#pragma once
#include "bw/input/physical_state.hpp"
#include "menu_bindings.hpp"
namespace bw::game {
struct MappedInput {
  input::PhysicalState physical;
  std::uint32_t pressed_actions{};
  std::uint32_t repeated_actions{};
  bool close_requested{};
  // Availability admits device configuration; active family selects prompts.
  bool controller_connected{};
  MenuInputDevice prompt_device{MenuInputDevice::keyboard_mouse};
  // Fixed UI receiver axis, populated by the game adapter from positional
  // stick Y plus wheel impulses. Independent of saved gameplay bindings.
  float menu_vertical_axis{};
  // The game binding translator populates the following intentions from the
  // physical sample and authoritative settings. Gameplay owns dead zones,
  // rates and acceptance; menu repeats are separate.
  std::array<float, 2> movement{}, look{}, mouse_look{};
  static constexpr unsigned kDefaultSensitivity = 60;
  unsigned turn_speed{kDefaultSensitivity}, mouse_bias{kDefaultSensitivity};
  bool evade_held{}, pause_pressed{}, crouch_pressed{};
  // Copied binding results carry no shot acceptance or cadence.
  bool keyboard_magic_pressed{}, keyboard_cycle_forward_pressed{},
      weapon_cycle_backward_pressed{};
  float fire_trigger{};
  // Presses survive a down/up pair drained in one pump. These are not
  // menu-repeat actions; saved bindings are interpreted by game code.
  bool reload_pressed{}, attack_pressed{}, weapon_cycle_pressed{},
      aim_mode_pressed{};
  // Translated shoulder and face intentions retain separate holds and edges.
  // Game policy decides chords, selection priority and consumption.
  bool shoulder_left_pressed{}, shoulder_right_pressed{};
  bool shoulder_left_held{}, shoulder_right_held{};
  bool face_north_pressed{}, face_east_pressed{}, face_south_pressed{},
      face_west_pressed{};
};
}  // namespace bw::game
