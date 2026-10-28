// Shared source cadence verifies catch-up receipts, idle-debt discard and
// terminal-empty semantics independently of either player or NPC input.
#include "game/combat/weapon_resources.hpp"

#include <cmath>
#include <limits>

#include "../../../fixtures/camera_fixture.hpp"
void weapon_resource_tests() {
  using bw::game::ShotStatus;
  using bw::game::WeaponResources;
  using camera_fixture::require;
  WeaponResources weapon(3, 10);
  auto receipt = weapon.try_fire();
  require(receipt.status == ShotStatus::accepted && receipt.shot_count == 1 &&
              receipt.first_shot_age_seconds == 0 &&
              weapon.snapshot().ammunition == 2,
          "Shared magazine accepts initial shot with source age");
  require(weapon.try_fire().status == ShotStatus::cooling_down,
          "Repeated attempt cannot bypass cadence");
  weapon.advance(.35F);
  receipt = weapon.try_fire();
  require(receipt.status == ShotStatus::empty && receipt.shot_count == 2 &&
              std::abs(receipt.first_shot_age_seconds - .25F) < .00001F &&
              receipt.interval_seconds == .1F &&
              weapon.snapshot().ammunition == 0,
          "Terminal empty retains both earlier shots and their scheduled ages");
  weapon.recharge();
  require(weapon.snapshot().ammunition == 3 &&
              weapon.snapshot().cooldown_seconds == 0,
          "Recharge atomically restores magazine and cadence");
  (void)weapon.try_fire();
  weapon.advance(.2F);
  weapon.advance(.2F);
  require(weapon.try_fire().shot_count == 1,
          "Idle child update discards previous overshoot instead of "
          "accumulating debt");
  const auto before = weapon.snapshot();
  bool rejected = false;
  try {
    weapon.advance(std::numeric_limits<float>::infinity());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(
      rejected && weapon.snapshot().cooldown_seconds == before.cooldown_seconds,
      "Invalid delta rejects before cadence changes");
}
