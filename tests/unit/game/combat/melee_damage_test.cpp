// Radial melee differs from body-scaled bullets and has separate death tables.
#include "game/combat/melee_damage.hpp"

#include <cmath>
#include <stdexcept>

#include "game/actors/animation/melee_blast.hpp"
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace
void melee_damage_tests() {
  using bw::game::accepted_melee_death;
  using bw::game::AcceptedMeleeDeathPresentation;
  using bw::game::ActorDamageDefinition;
  using bw::game::melee_direction;
  using bw::game::melee_magnitude;
  using bw::game::melee_reaction;
  using bw::game::MeleeImpact;
  using bw::game::MeleeReaction;
  using bw::game::MeleeReactionObservation;
  MeleeImpact impact{.source_position = bw::math::Vector3<float>{2, 0, 0},
                     .center = {},
                     .contact = {.5F, 0, 0},
                     .near_damage = 100,
                     .far_damage = 300,
                     .inner_radius = 0,
                     .outer_radius = 1};
  require(melee_magnitude(impact) == 200, "Melee radial interpolation");
  impact.contact = {2, 0, 0};
  require(melee_magnitude(impact) == 300, "Melee far radius clamp");
  impact.contact = {};
  require(melee_magnitude(impact) == 100, "Melee inner radius clamp");
  impact.outer_radius = 0;
  require(melee_magnitude(impact) == 100, "Melee zero radius source branch");
  require(melee_direction(impact, {}) == bw::math::Vector3<float>{-2, 0, 0},
          "Melee direction uses actual attacker position");
  impact.source_position.reset();
  impact.contact = {0, 0, 3};
  require(melee_direction(impact, {}) == bw::math::Vector3<float>{0, 0, -3},
          "Melee invalid attacker uses contact position");
  ActorDamageDefinition definition;
  definition.melee_heavy_threshold = 200;
  MeleeReactionObservation observation{.health = 1, .magnitude = 200};
  require(melee_reaction(definition, observation, 0) == MeleeReaction::ordinary,
          "Threshold equality must retain ordinary melee reaction");
  observation.magnitude = 201;
  require(melee_reaction(definition, observation, 0) == MeleeReaction::heavy,
          "Melee heavy threshold");
  observation.crouched = true;
  require(melee_reaction(definition, observation, 0) == MeleeReaction::crouched,
          "Crouched melee overrides magnitude");
  observation.reaction_blocked = true;
  require(melee_reaction(definition, observation, 0) == MeleeReaction::none,
          "Blast state suppresses surviving melee reaction");
  observation.health = 0;
  require(
      melee_reaction(definition, observation, 0) == MeleeReaction::death_motion,
      "Melee death is selected before surviving blast-state gating");
  observation.physical_death_family = true;
  require(melee_reaction(definition, observation, .49F) ==
                  MeleeReaction::death_blast_start &&
              melee_reaction(definition, observation, .5F) ==
                  MeleeReaction::death_motion,
          "Source physical family half-probability boundary");
  AcceptedMeleeDeathPresentation death;
  death.direction.incoming = {0, 0, 1};
  death.weights = {1, 1};
  require(accepted_melee_death(death).requests[0].slot == 745,
          "Positive melee first death slot");
  death.random_unit = .5F;
  require(accepted_melee_death(death).requests[0].slot == 747,
          "Positive melee strict weighted boundary");
  death.direction.incoming = {0, 0, -1};
  require(accepted_melee_death(death).requests[0].slot == 748,
          "Nonpositive melee second death slot");
  death.random_unit = 0;
  require(accepted_melee_death(death).requests[0].slot == 746,
          "Nonpositive melee first death slot");
  bw::game::MeleeBlast blast;
  const auto model = bw::math::identity_matrix<float, 4>();
  const auto start = blast.begin({0, 0, -1}, model);
  require(
      start.slot == 844 && blast.phase() == bw::game::MeleeBlastPhase::start,
      "Melee blast must start with original directional launch motion");
  require(!blast.finish({.grounded = true}).motion,
          "Grounding cannot skip blast launch completion");
  const auto flight = blast.finish({.motion_completed = true});
  require(
      flight.motion && flight.motion->slot == 843 && !flight.physical_handoff,
      "Blast launch completion must retain animated flight");
  require(blast.displacement({0, .1F, 0}, .1F, false)[1] == .1F,
          "Flight must preserve authored vertical root before completion");
  (void)blast.displacement({0, 0, 0}, .1F, true);
  require(std::abs(blast.displacement({}, .1F, false)[1] + .049F) < .00001F,
          "Post-animation flight must integrate source gravity");
  const auto landing = blast.finish({.grounded = true});
  require(landing.motion && landing.motion->slot == 842 &&
              landing.release_hit_flags,
          "Ground contact must enter landing and clear source query flags");
  require(
      !blast.finish({.motion_completed = true}).physical_handoff,
      "Melee landing death is frame-crossing owned, not motion-end fallback");
  require(blast.finish({.crossed_death_frame = true}).physical_handoff &&
              !blast.active(),
          "Landing frame five must transfer actual body authority");
  require(blast.begin({0, 0, 1}, model).slot == 841,
          "Opposing melee launch must use original rear entry");
  require(blast.begin({1, 0, 0}, model).slot == 847,
          "Left melee launch must use original side entry");
  require(blast.begin({-1, 0, 0}, model).slot == 850,
          "Right melee launch must use original side entry");
}
