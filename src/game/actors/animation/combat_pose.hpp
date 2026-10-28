// Original completed-pose observations shared by player and NPC combat.
// Borrow immutable completed matrices for one call and return copied values;
// no animation, clock, renderer or actor mutation. Weapon locators use prepared
// weighted control points and completed skin matrices; absent locators use the
// model frame unchanged. Aim height uses the original eye threshold, then the
// model-resource radius. Fixed work, no allocation. Core I.1 (#ri-explicit)
// distinguishes model data from collision dimensions; performance MEM.9 keeps
// these frame observations allocation free. I.4 keeps movement observations
// copied and typed. Both design corpora read; first-combat-ai.md records
// source.
#pragma once
#include <optional>
#include <span>

#include "bw/content/formats/control_points.hpp"
#include "bw/math/matrix.hpp"
namespace bw::game {
std::optional<math::Affine3<float>> original_attachment_pose(
    const formats::ControlPointDefinition* prepared,
    std::span<const math::Matrix4<float>> completed_skin,
    const math::Affine3<float>& model, std::int32_t attachment_slot);
math::Affine3<float> original_weapon_attachment_pose(
    const formats::ControlPointDefinition* prepared,
    std::span<const math::Matrix4<float>> completed_skin,
    const math::Affine3<float>& model, std::int32_t attachment_slot);
math::Vector3<float> original_actor_aim_point(
    const math::Vector3<float>& position, float completed_eye_height,
    std::optional<float> original_model_radius);
// Source NPC category is a completed-pose observation, not a flight enum.
// Explicit DamageReaction slots override height; future StateSquat ownership
// can supply its distinct active state without guessing from posture bits.
struct NpcMovementObservation {
  float actor_height{}, eye_height{}, model_radius{};
  bool squat_active{};
  std::optional<std::uint32_t> damage_motion{};
};
std::uint8_t original_npc_movement_category(const NpcMovementObservation&);
}  // namespace bw::game
