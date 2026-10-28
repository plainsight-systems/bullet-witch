// Shared source actor contact/model radius policy. The caller supplies a
// finite positive authored radius and copied life state. Small dead actors use
// twice that radius for custom collision support and the negative model offset;
// skeletal provider shapes retain their separate authored recipe dimensions.
// This pure value function retains no owner, allocates nothing and is O(1).
// Header design and implementation consulted Core I.1 (#ri-explicit), keeping
// life an explicit input, and performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), adding no tick
// storage. Source consumers: first-combat-physics-boundary.md, common actor
// publication, contact admission and cylinder height. No provider repair or
// pose offset.
#pragma once

namespace bw::game {
// Use the same derived radius for support and model publication.
constexpr float character_body_radius(float authored_radius, bool alive) {
  constexpr float kSmallBodyLimit = 1.F, kDeadBodyScale = 2.F;
  return !alive && authored_radius <= kSmallBodyLimit
             ? authored_radius * kDeadBodyScale
             : authored_radius;
}
}  // namespace bw::game
