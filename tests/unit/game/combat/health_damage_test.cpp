// Verify distinct current/ceiling semantics and source admission gates.
#include "game/combat/health_damage.hpp"

#include <limits>

#include "../../../fixtures/camera_fixture.hpp"
#include "game/actors/death_lifetime.hpp"
#include "game/combat/ordinary_hit_response.hpp"
void health_damage_tests() {
  using bw::game::apply_health_damage;
  using bw::game::HealthDamageInput;
  using bw::game::HealthDamageMode;
  using bw::game::HitReaction;
  using bw::game::ordinary_hit_voice;
  using camera_fixture::require;
  require(
      !ordinary_hit_voice(HitReaction::none) &&
          ordinary_hit_voice(HitReaction::ordinary) == 1U &&
          ordinary_hit_voice(HitReaction::sustained) == 1U &&
          ordinary_hit_voice(HitReaction::additive) == 0x13U &&
          ordinary_hit_voice(HitReaction::death_motion) == 0U &&
          ordinary_hit_voice(HitReaction::death_physics) == 1U,
      "Source ordinary hit branches preserve distinct victim voice actions");
  HealthDamageInput input{80, 100, 100, 10, 1};
  auto result = apply_health_damage(input);
  require(result.accepted && result.alive && result.current == 90 &&
              result.recoverable == 90,
          "Ordinary health adopts reduced recoverable ceiling");
  input.mode = HealthDamageMode::player_camera;
  result = apply_health_damage(input);
  require(result.current == 40 && result.recoverable == 90,
          "Camera actor preserves separate four-times current debit");
  input.invulnerable = true;
  result = apply_health_damage(input);
  require(!result.accepted && result.alive && result.current == 80 &&
              result.recoverable == 100,
          "Invulnerable gate preserves both pools and delay-reset receipt");
  input.invulnerable = false;
  input.current = 0;
  result = apply_health_damage(input);
  require(!result.accepted && !result.alive && result.recoverable == 100,
          "Already-dead gate does not revive from recoverable ceiling");
  input.current = 20;
  input.nonlethal = true;
  result = apply_health_damage(input);
  require(result.accepted && result.alive && result.current == 1 &&
              result.recoverable == 100,
          "Source nonlethal branch restores ceiling and one current point");
  input.magnitude = std::numeric_limits<float>::infinity();
  bool rejected = false;
  try {
    (void)apply_health_damage(input);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Nonfinite damage rejects before resource commit");
  bw::game::DeathLifetime corpse;
  bw::game::DeathRetirementObservation view{
      {0, 0, -6}, {}, {0, 0, 1}, 2, false};
  corpse.advance(5.5F);
  require(!corpse.retirement_due(view),
          "Source corpse delay keeps an otherwise eligible dead actor");
  corpse.advance(0);
  require(!corpse.retirement_due(view),
          "A paused death owner cannot age its corpse");
  corpse.advance(.5F);
  require(corpse.retirement_due(view),
          "Six seconds admits retirement beyond five units behind camera");
  view.position[2] = -5;
  require(!corpse.retirement_due(view), "Corpse distance boundary is strict");
  view.position[2] = 50;
  require(!corpse.retirement_due(view),
          "Visible-forward corpses stay resident");
  view.position[2] = -50;
  view.camera_owned = true;
  require(!corpse.retirement_due(view), "Player camera owner is exempt");
  view.camera_owned = false;
  view.source_family_flags = 0x102;
  view.position[2] = -40;
  require(!corpse.retirement_due(view),
          "Extended source family retains corpses through forty units");
  view.position[2] = -41;
  require(corpse.retirement_due(view), "Extended source distance is bounded");
}
