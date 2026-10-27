// Native cockpit publication adapter: derive lanes/receipts from observations,
// then run original presentation only on an active admitted world frame.
// Implementation I.1/E.4 and MEM.9 keep authority outside presentation and
// fixed snapshot translation outside the launch/platform loop.
#include "player_cockpit.hpp"

#include <stdexcept>

namespace bw::game {
PlayerCockpit::PlayerCockpit(GameplayHudOutput& surface, SourceRandom& random,
                             CockpitActivation activation)
    : surface_(surface), random_(random), activation_(activation) {
  if (activation != CockpitActivation::hidden &&
      activation != CockpitActivation::inspection &&
      activation != CockpitActivation::script) {
    throw std::invalid_argument("Invalid cockpit activation");
  }
}
void PlayerCockpit::advance(const world::WorldFrame& frame,
                            const PlayerResourceSnapshot& resources,
                            bool aiming, const SpellRingPresentation& ring,
                            const MissionGoalObservation* goal_observation) {
  if (activation_ == CockpitActivation::hidden ||
      !frame.admission().simulation) {
    return;
  }
  const auto& weapon = resources.weapons.at(resources.selected_weapon);
  hud_.advance({.health = {resources.health, resources.recoverable_health,
                           resources.maximum_health},
                .magic = {resources.magic, resources.recoverable_magic,
                          resources.maximum_magic},
                .ammunition = weapon.ammunition,
                .ammunition_capacity = weapon.capacity,
                .weapon = static_cast<std::uint8_t>(resources.selected_weapon),
                .recharged = (resources.recharged_weapons &
                              (1U << resources.selected_weapon)) != 0,
                .aiming = aiming,
                .magic_ring = ring,
                .goal_observation = goal_observation},
               frame.delta_seconds(), random_);
}
void PlayerCockpit::show(std::string_view diagnostic) {
  if (activation_ == CockpitActivation::hidden) {
    surface_.hide();
  } else {
    surface_.show(hud_, diagnostic);
  }
}
}  // namespace bw::game
