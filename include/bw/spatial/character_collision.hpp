// Shared upright-character contact contract; game state owns desired movement,
// gravity, animation and jump initiation. Positions are lower-sphere centers in
// world units, Y up; cylinder_height is the distance between hemisphere
// centers. StaticCollision prepares immutable geometry once, then resolves
// previous to proposed movement, body penetration and qualified floor support.
// Finite inputs, positive radius/dt and nonnegative cylinder height are
// required; invalid input throws before publishing a result. Returned values
// own no scene memory. Frozen queries are reentrant and allocate no
// successful-path heap storage. Optimization: setup-time triangle bounds/BVH
// avoid scanning the stage each tick; bounded local scratch is query-owned.
// Work scales with intersecting BVH nodes/triangles and bounded correction
// passes. Source behavior and explicit adaptations:
// research/engine/subsystems/player-collision-boundary.md. Design MCP: I.11
// (#ri-raw) returns values without transferring raw ownership. Both phases read
// SL.con.3
// (#rsl-bounds) bounds ranges; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) confines geometry allocation to
// preparation. Contact-mode design/implementation E.4 (#re-design-invariants)
// distinguishes state-owned grounded support from ballistic ascent; MEM.9 keeps
// this policy a value in the existing allocation-free query.
#pragma once

#include <array>
#include <cstdint>

namespace bw::scene {
// Selected by the movement state, independently of its previous contact result.
enum class CharacterContactMode : std::uint8_t { grounded, airborne };
struct CharacterCollisionRequest {
  static constexpr std::uint32_t kDefaultExcludedFlags = 0x100;
  std::array<float, 3> previous_position{}, proposed_position{};
  float radius{}, cylinder_height{}, delta_seconds{}, vertical_velocity{};
  bool previously_grounded{};
  std::uint32_t excluded_object_flags{kDefaultExcludedFlags};
  // Game policy may extend downward following while already grounded; this
  // never enlarges upward step correction. Airborne ascent suppresses support;
  // positive authored root velocity does not suppress grounded support.
  float ground_probe_extension{};
  CharacterContactMode mode{CharacterContactMode::grounded};
};
struct CharacterCollisionResult {
  std::array<float, 3> position{}, ground_normal{};
  bool grounded{}, hit_wall{}, hit_ceiling{};
};
}  // namespace bw::scene
