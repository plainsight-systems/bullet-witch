// Original player input owns ring priority/consumption. Preserve the one
// selection lifecycle and publish a typed missing-owner result, never a cast.
// Implementation Core I.1/E.4 and performance MEM.9 were reread: fixed copied
// observations and retained state, no capability to mutate resources or time.
#include "player_input_routing.hpp"

namespace bw::game {
PlayerInputResult PlayerInputRouting::route(const world::WorldFrame& frame,
                                            GameplayInput input,
                                            const MissionPlayerState& mission,
                                            const PlayerResources& resources) {
  if (!frame.admission().simulation) return {input, {}, {}};
  SpellAvailabilitySet availability;
  for (unsigned index = 0; index < availability.size(); ++index) {
    availability.at(index) = {
        mission.levels().spells.at(index) != 0,
        resources.can_afford_spell(static_cast<PlayerSpell>(index))};
  }
  const bool ring_input = selection_.active() || input.shoulder_left_pressed ||
                          input.shoulder_right_pressed;
  const bool expire =
      input.shoulder_left_held && input.shoulder_right_held &&
      (input.shoulder_left_pressed || input.shoulder_right_pressed);
  std::uint8_t choice = 0;
  if (ring_input) {
    if (input.face_north_pressed) {
      choice = 1;
    } else if (input.face_east_pressed) {
      choice = 2;
    } else if (input.face_south_pressed) {
      choice = 3;
    } else if (input.face_west_pressed) {
      choice = 4;
    }
  }
  const auto selection = selection_.advance(
      {.previous_page = input.shoulder_left_pressed && !expire,
       .next_page = input.shoulder_right_pressed && !expire,
       .choice = choice,
       .cancel = ring_input && input.aim_mode_pressed,
       .expire = expire},
      availability, frame.delta_seconds());
  if (ring_input) {
    input.reload_pressed = false;
    input.attack_pressed = false;
    input.weapon_cycle_pressed = false;
    input.aim_mode_pressed = false;
  }
  const auto status = selection.rejected ? SpellRequestStatus::unavailable
                      : selection.requested
                          ? SpellRequestStatus::missing_world_owner
                          : SpellRequestStatus::none;
  return {input, status, selection.requested};
}
}  // namespace bw::game
