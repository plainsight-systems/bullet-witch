// Copied ordinary-hit feedback policy, independent of presentation and actors.
// Incoming direction points along the projectile; camera forward supplies its
// current horizontal reference. Nonfinite values or a vertical-only camera
// axis reject. The original zero horizontal hit direction selects left. No
// state, resource borrows or allocations. Core I.1 (#ri-explicit) separates
// combat classification from HUD lifetime; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps fixed value
// work.
#pragma once
#include <cstdint>

#include "bw/math/vector.hpp"
namespace bw::game {
enum class DamageDirection : std::uint8_t { front, right, rear, left };
DamageDirection classify_damage_direction(
    const math::Vector3<float>& incoming,
    const math::Vector3<float>& camera_forward);
}  // namespace bw::game
