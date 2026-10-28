// Shared Shooter accepted-shot animation messages. The caller has admitted the
// original gun action; this policy neither spends ammunition nor emits a hit.
// Source630400 derives upper discharge from class/stance/body direction, then
// weapon slot6, then recoil. Camera attachment selects player body-family
// recoil; actors without that attachment use slot0x2c1, independently of ground
// contact. Return fixed copied requests; catalogs, playback, time and physics
// remain actor/world owned. Invalid source class/slot rejects; no allocation.
// Core I.11 (#ri-raw) and performance CDSA.32, read in both phases, share
// policy without borrowing a player controller or mutable actor state.
#pragma once
#include "action_motion_plan.hpp"

namespace bw::game {
struct AcceptedWeaponShotMotion {
  std::uint32_t body_slot{}, weapon_class{};
  bool crouched{}, player_camera{};
};
ActionMotionPlan accepted_weapon_shot_motion(AcceptedWeaponShotMotion);
struct AcceptedWeaponReloadMotion {
  std::uint32_t body_slot{}, weapon_class{};
  bool crouched{};
};
struct AcceptedWeaponReadyMotion {
  std::uint32_t body_slot{};
  bool crouched{}, raised{};
};
// These are accepted state changes; caller filters repeated ready state and
// commits reload resources. Actor catalogs resolve returned source identities.
ActionMotionPlan accepted_weapon_reload_motion(AcceptedWeaponReloadMotion);
ActionMotionPlan accepted_weapon_ready_motion(AcceptedWeaponReadyMotion);
}  // namespace bw::game
