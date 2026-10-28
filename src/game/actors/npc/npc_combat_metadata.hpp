// Prepare immutable original actor-environment fields at mission admission.
// The placement definition and archetype are borrowed only during decoding;
// the result owns numeric values and a profile, never source pointers. Decode
// authored ranges, perception, voice and path loop once, preserving the source
// distinction between constructor defaults and placement defaults. Group is a
// live placement-group value supplied by the admitting world. Invalid rows or
// nonfinite values fail before publication. Fixed work plus bounded path-row
// lookup, no allocation or per-frame parsing. Core I.1 (#ri-explicit) exposes
// the producer; performance CDSA.32
// (transform-static-data-into-the-consumer-layout) prepares copied consumer
// data. Both design corpora read. Source mappings: first-combat-ai.md and
// first-combat-actor-audio.md.
#pragma once
#include "ai_settings.hpp"
#include "game/actors/audio/character_audio.hpp"
#include "game/content/original/mission_placement.hpp"
namespace bw::game {
struct NpcCombatMetadata {
  NpcActorAiSettings ai;
  CharacterAudioProfile audio;
  std::uint32_t affiliation{};
  float movement_rate_scale{};
  std::int32_t ai_mode{};
  bool leader{};
};
NpcCombatMetadata prepare_npc_combat_metadata(
    const content::MissionPlacementDefinition&,
    const content::PlacementArchetype&, std::int32_t group);
// Original script-facing model classification of a copied actor affiliation.
// Unknown categories return -1, never the generic soldier category.
std::int32_t npc_target_model(std::uint32_t affiliation) noexcept;
}  // namespace bw::game
