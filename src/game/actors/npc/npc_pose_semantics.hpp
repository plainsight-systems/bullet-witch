// EA0's traced common Runner/Shooter/Damager receivers define pose semantics;
// unrelated families and unclassified clips retain unknown. Exact canonical
// slot labels must match before granting ordinary/additive interpretation.
// The prepared catalog owns names/keys; this setup-only function borrows it and
// the caller's unknown-initialized output. O(aliases), no allocation or actor
// mutation. Core I.11 (#ri-raw), performance CDSA.32
// (transform-static-data-into- the-consumer-layout), both design corpora read.
// See first-combat-ai.md.
#pragma once
#include "game/actors/animation/character_animations.hpp"
namespace bw::game {
void npc_pose_semantics(const animation::PreparedClipCatalog&, std::string_view,
                        std::span<MotionPoseSemantics>);
}  // namespace bw::game
