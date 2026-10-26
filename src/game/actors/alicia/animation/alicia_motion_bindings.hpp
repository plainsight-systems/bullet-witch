// Original Alicia alias publication policy. Return a borrowed immutable table;
// callers keep no ownership and cannot mutate it. Preserve source slot/write
// order including repeated aliases and absent clips. No allocation, O(1) view.
// Core I.1 (#ri-explicit), SF.11 (#rs-contained) exposes actor policy
// explicitly; performance COPY.7 (eliminate-hidden-copies), both phases, avoids
// a table copy. Source provenance: alicia-animation-coverage.md.
#pragma once
#include "bw/animation/clip_catalog.hpp"
namespace bw::game {
// Original global/PW slot mapping is compatibility policy, not a universal
// game-action enum. Missing motion slots remain absent even if parameters
// exist.
std::span<const animation::ClipAliasRequest> alicia_motion_aliases();
}  // namespace bw::game
