// Original AI contact-action selection, independent of actor/world ownership.
// The caller supplies the source-relative threat angle and completed sensor
// results; selection tests exactly three authored directions, falling back to
// back when none qualify. Grapple modes select fixed original animation slots.
// No allocation, random stream, local clock or physics mutations. Core I.1
// (#ri-explicit) exposes source observations; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps bounded value operations. Both
// design corpora read; first-combat-ai.md records native contracts.
#pragma once
#include <array>
#include <optional>

#include "bw/math/vector.hpp"
#include "game/actors/animation/action_motion_plan.hpp"
namespace bw::game {
std::array<unsigned, 3> npc_dodge_order(float threat_angle_radians);
std::uint32_t npc_dodge_slot(const std::array<unsigned, 3>& order,
                             const std::array<bool, 4>& clear);
std::optional<ActionMotionRequest> npc_grapple_motion(std::uint32_t mode);
// Preserve original raw-displacement alignment before angular limiting.
// Collinear inputs that make the native cross-axis undefined use the pinned
// Unreal FixedTurn tie convention; no NaN reaches an actor transform.
math::Vector3<float> npc_grapple_direction(
    const math::Vector3<float>& unit_forward,
    const math::Vector3<float>& target_displacement, float maximum_angle);
}  // namespace bw::game
