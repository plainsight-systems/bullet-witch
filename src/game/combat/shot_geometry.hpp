// Original straight-shot geometry, separate from emission/resource ownership.
// Copied camera/actor/bone observations produce the camera query segment,
// corrected launch origin and independent scenery reference. World combat owns
// queries, side filtering, shot ages and admission; these helpers cannot step
// physics, mutate actors or retain borrowed observations. Original native
// stages and constants are documented in first-combat-physics-boundary.md.
// All vectors/scalars must be finite; camera forward must be unit, range and
// actor radius positive. Invalid/degenerate inputs throw before consuming RNG.
// Spread sampling consumes two draws even for a zero cone. Collinear basis
// and opposite cone directions use the recovered original axis policy.
// Spread evolution returns a copied scalar; caller owns per-weapon state and
// supplies the source movement-category widening bit and world delta.
// Fixed work/storage and no valid-path allocation, locking or hidden clock.
// Design Core I.1 (#ri-explicit) names all observations and mutation;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// bounds scratch.
#pragma once
#include "bw/math/matrix.hpp"
#include "weapon_definition.hpp"

namespace bw::game {
class SourceRandom;
struct ShotSegment {
  math::Vector3<float> start{}, end{};
};
struct PlayerShotView {
  math::Vector3<float> camera_position{}, camera_forward{}, actor_position{};
  float range{};
};
ShotSegment player_shot_segment(const PlayerShotView&);
struct ShotGeometry {
  math::Vector3<float> origin{}, target{}, scenery_reference{};
};
// Source Shooter fallback adds the returned weapon locator Z axis to its
// origin. Explicit target requests replace this target before admission.
ShotGeometry weapon_forward_shot_geometry(
    const math::Affine3<float>& weapon_locator_world);
struct PlayerShotPlacement {
  // Original unnormalized row2 of the projectile-origin bone. Authored
  // quaternion magnitude survives here; normalization changes source placement.
  math::Vector3<float> muzzle{}, target{}, actor_position{}, weapon_forward{};
  float actor_radius{};
  bool aim_hit{};
};
ShotGeometry player_shot_geometry(const PlayerShotPlacement&);
// Original locomotion publishes widening only for body families0x10/0x290.
bool weapon_spread_widens(std::size_t body_slot) noexcept;
float advance_weapon_spread(const WeaponSpread&, float current_radians,
                            float elapsed_seconds, bool widening);
math::Vector3<float> sample_shot_direction(const ShotGeometry&,
                                           float spread_radians, SourceRandom&);
math::Matrix4<float> shot_forward_transform(
    const math::Vector3<float>& origin, const math::Vector3<float>& direction);
}  // namespace bw::game
