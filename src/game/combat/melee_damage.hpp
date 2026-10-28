// Original radial melee receipt, distinct from regional projectile damage.
// World owns identity/line-of-sight/effect admission; actor owners commit
// health and animation. This policy borrows immutable prepared damage values
// and consumes copied facts only. Core I.4 (#ri-typed) separates radial
// geometry from damage outcome; CDSA.32 keeps authored thresholds prepared once
// rather than parsing during a hit. Fixed work/storage and no retained
// capabilities. Source685170/5fd090/6881a0; first-combat-physics-boundary.md.
#pragma once
#include <optional>

#include "damage_definition.hpp"
#include "game/actors/animation/character_reaction_policy.hpp"
namespace bw::game {
struct MeleeImpact {
  std::uint64_t source{}, target{};
  std::optional<math::Vector3<float>> source_position{};
  math::Vector3<float> center{}, contact{};
  float near_damage{}, far_damage{}, inner_radius{}, outer_radius{};
};
float melee_magnitude(const MeleeImpact&);
// Source uses the attacker position when its identity remains valid, otherwise
// the contact; its incoming direction points from that position to the victim.
math::Vector3<float> melee_direction(const MeleeImpact&,
                                     math::Vector3<float> victim_position);
struct MeleeReactionObservation {
  float health{}, magnitude{};
  bool reactions_enabled{true}, ordinary_shooter{}, reaction_blocked{};
  bool crouched{}, physical_death_family{};
};
enum class MeleeReaction : std::uint8_t {
  none,
  ordinary,
  heavy,
  crouched,
  death_motion,
  death_blast_start
};
// Lethal selection consumes one draw even for a nonphysical family, matching
// the native predicate order. Death slot selection consumes a separate draw.
MeleeReaction melee_reaction(const ActorDamageDefinition&,
                             const MeleeReactionObservation&, float death_draw);
}  // namespace bw::game
