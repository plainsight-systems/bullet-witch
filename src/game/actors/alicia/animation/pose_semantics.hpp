// Alicia/weapon receiver representation policy, prepared once against immutable
// catalog identity. The caller supplies one unknown-initialized slot per clip;
// this function grants only source-witnessed ordinary/additive semantics and
// rejects mismatched output size. Borrowed catalog/output live through the
// call. No allocation; O(aliases + explicit weapon names), never per-frame
// work. Core I.1 (#ri-explicit) makes receiver policy an explicit constructor
// input; performance CDSA.32 (transform-static-data-into-the-consumer-layout)
// stores one byte per prepared clip. Both corpora read in
// design/implementation. Existing source mapping: animation-review-geometry.md
// and alicia-animation-coverage.md.
#pragma once
#include "game/actors/animation/character_animations.hpp"
namespace bw::game {
void alicia_pose_semantics(const animation::PreparedClipCatalog& catalog,
                           std::string_view owner,
                           std::span<MotionPoseSemantics> result);
}  // namespace bw::game
