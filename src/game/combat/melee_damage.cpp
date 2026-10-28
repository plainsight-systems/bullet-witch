// Source radial interpolation and state selection consume immutable prepared
// values. Core I.4/CDSA.32 were reread for implementation: copied typed facts,
// no actor mutation or raw data parsing in the policy.
#include "melee_damage.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bw::game {
namespace {
constexpr float kMinimumRadius = .0001F;
void validate(const MeleeImpact& hit) {
  if (!math::all_finite(hit.center) || !math::all_finite(hit.contact) ||
      (hit.source_position && !math::all_finite(*hit.source_position)) ||
      !std::isfinite(hit.near_damage) || hit.near_damage < 0 ||
      !std::isfinite(hit.far_damage) || hit.far_damage < 0 ||
      !std::isfinite(hit.inner_radius) || hit.inner_radius < 0 ||
      !std::isfinite(hit.outer_radius) || hit.outer_radius < 0 ||
      (hit.outer_radius >= kMinimumRadius &&
       hit.inner_radius >= hit.outer_radius)) {
    throw std::invalid_argument("Invalid source melee impact");
  }
}
}  // namespace
float melee_magnitude(const MeleeImpact& hit) {
  validate(hit);
  if (hit.outer_radius < kMinimumRadius) return hit.near_damage;
  const auto distance = math::length(math::subtract(hit.contact, hit.center));
  if (!std::isfinite(distance)) {
    throw std::overflow_error("Melee radial distance overflow");
  }
  const auto blend = std::clamp(
      (distance - hit.inner_radius) / (hit.outer_radius - hit.inner_radius),
      0.F, 1.F);
  return (1.F - blend) * hit.near_damage + hit.far_damage * blend;
}
math::Vector3<float> melee_direction(const MeleeImpact& hit,
                                     math::Vector3<float> victim_position) {
  validate(hit);
  const auto result = math::subtract(victim_position,
                                     hit.source_position.value_or(hit.contact));
  if (!math::all_finite(result)) {
    throw std::invalid_argument("Invalid melee reaction direction");
  }
  return result;
}
MeleeReaction melee_reaction(const ActorDamageDefinition& definition,
                             const MeleeReactionObservation& observation,
                             float death_draw) {
  if (!std::isfinite(observation.health) || observation.health < 0 ||
      !std::isfinite(observation.magnitude) || observation.magnitude < 0 ||
      !std::isfinite(death_draw) || death_draw < 0 || death_draw > 1) {
    throw std::invalid_argument("Invalid melee reaction observation");
  }
  constexpr float kPhysicalProbability = .5F;
  if (observation.health == 0) {
    return observation.physical_death_family &&
                   death_draw < kPhysicalProbability
               ? MeleeReaction::death_blast_start
               : MeleeReaction::death_motion;
  }
  if (!observation.reactions_enabled ||
      (!observation.ordinary_shooter && observation.reaction_blocked)) {
    return MeleeReaction::none;
  }
  if (observation.crouched) return MeleeReaction::crouched;
  return observation.magnitude > definition.melee_heavy_threshold
             ? MeleeReaction::heavy
             : MeleeReaction::ordinary;
}
}  // namespace bw::game
