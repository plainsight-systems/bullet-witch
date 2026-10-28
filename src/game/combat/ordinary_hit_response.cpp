// Preserve original RNG call placement, including the low-health draw for
// families that subsequently reject sustained reactions. No actor state is
// changed here. Core I.11/CDSA.32 were reread for this implementation phase.
#include "ordinary_hit_response.hpp"

#include <cmath>
#include <stdexcept>
namespace bw::game {
std::optional<std::uint32_t> ordinary_hit_voice(HitReaction reaction) {
  // Source ordinary-hit branches stop the prior voice before these distinct
  // requests. Animation events and scenery impact cues are separate producers.
  constexpr std::uint32_t kAdditiveDamageVoice = 0x13;
  switch (reaction) {
    case HitReaction::none:
      return std::nullopt;
    case HitReaction::additive:
      return kAdditiveDamageVoice;
    case HitReaction::ordinary:
    case HitReaction::sustained:
    case HitReaction::death_physics:
      return 1;
    case HitReaction::death_motion:
      return 0;
  }
  throw std::invalid_argument("Unknown ordinary hit voice response");
}
HitReaction ordinary_hit_response(const ActorDamageDefinition& definition,
                                  const OrdinaryHitObservation& observation,
                                  SourceRandom& random) {
  if (!std::isfinite(observation.health) || observation.health < 0 ||
      !std::isfinite(observation.maximum_health) ||
      observation.maximum_health <= 0 ||
      !std::isfinite(observation.magnitude) || observation.magnitude < 0 ||
      observation.body >= definition.ordinary.size() ||
      observation.movement_category > 2) {
    throw std::invalid_argument("Invalid ordinary hit response observation");
  }
  if (observation.magnitude > definition.heavy_damage_threshold) {
    throw std::invalid_argument(
        "Heavy damage needs its original response receiver");
  }
  if (observation.health == 0) {
    return observation.movement_category == 2 ? HitReaction::death_physics
                                              : HitReaction::death_motion;
  }
  if (!observation.reactions_enabled || observation.blast_active) {
    return HitReaction::none;
  }
  if (observation.health <
          definition.sustained_health_ratio * observation.maximum_health &&
      random.unit() < definition.sustained_probability &&
      observation.sustained_family && !observation.sustained_forbidden) {
    return HitReaction::sustained;
  }
  if (!observation.damage_reaction_active &&
      random.unit() <=
          definition.ordinary.at(observation.body).reaction_threshold &&
      observation.movement_category < 1) {
    return HitReaction::ordinary;
  }
  return HitReaction::additive;
}
}  // namespace bw::game
