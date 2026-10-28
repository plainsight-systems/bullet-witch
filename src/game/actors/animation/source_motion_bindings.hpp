// Original shared character slot policy and explicit player substitutions.
// Borrow immutable process-lifetime views; no clip/resource ownership, action
// state or clock is transferred. Source601250's global names and explicit
// player substitutions are composed at compile time into the existing ordered
// player result. Missing slots remain absent. NPCs request only the common
// policy and cannot inherit Alicia reload/evade substitutions. O(1) lookup
// view, no runtime allocation. Core I.11 (#ri-raw) and performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// were retrieved for design. See alicia-animation-coverage.md and
// first-combat-ai.md.
#pragma once
#include "bw/animation/clip_catalog.hpp"
namespace bw::game {
std::span<const animation::ClipAliasRequest> common_motion_aliases();
std::span<const animation::ClipAliasRequest> player_motion_aliases();
// EA0/EA1/EA2 share the original soldier substitutions. WG1 adds no weapon
// overrides; other soldier weapon families require their own source tables.
std::span<const animation::ClipAliasRequest> soldier_motion_aliases();
}  // namespace bw::game
