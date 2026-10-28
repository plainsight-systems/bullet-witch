// Ordinary bullet reaction selection after the health transaction. Actor owns
// health and active states; this policy consumes copied facts, authored region
// probability and the shared source RNG, then returns an explicit response.
// No physics, animation playback, clock or mutation capability is retained.
// The admitted PW0/EA0 ordinary guns stay below heavy-hit thresholds. Other
// impact classes require their source receiver and reject here. Source order:
// lethal movement branch, disabled/blast gate, rare sustained draw, ordinary
// reaction draw, then additive fallback. Fixed work and no allocation. Both
// phases Core I.11 (#ri-raw) and CDSA.32
// (transform-static-data-into-the-consumer- layout) preserve resident policy
// and per-actor observations. Source6867b0, 687d30/688240; RTTI identifies
// 5fdc80=StateBlast, 5fdd40=StateDamageReaction.
#pragma once
#include <optional>

#include "damage_definition.hpp"
#include "game/session/compatibility/source_random.hpp"

namespace bw::game {
enum class HitReaction : std::uint8_t {
  none,
  additive,
  ordinary,
  sustained,
  death_motion,
  death_physics
};
// Actor response publishes only the original requested voice, not an audio
// owner. The world forwards it through that actor's retained voice authority.
struct HitReception {
  bool accepted{};
  std::optional<std::uint32_t> voice_action;
  // First living-to-dead transition only; subsequent corpse hits cannot award.
  bool defeated{};
};
std::optional<std::uint32_t> ordinary_hit_voice(HitReaction);
struct OrdinaryHitObservation {
  float health{}, maximum_health{}, magnitude{};
  std::uint8_t body{};
  unsigned movement_category{};
  bool reactions_enabled{true}, blast_active{}, damage_reaction_active{};
  bool sustained_family{}, sustained_forbidden{};
};
HitReaction ordinary_hit_response(const ActorDamageDefinition&,
                                  const OrdinaryHitObservation&, SourceRandom&);
}  // namespace bw::game
