// Preserve source weapon/aim observation semantics before shot construction.
// Missing locator lookup follows the original model-frame fallback. Weighted
// attachment frames receive the source half turn. Aim-height inputs
// remain model observations, never substituted collider heights. Core E.4
// rejects nonfinite inputs; MEM.9 uses fixed matrices without frame allocation.
// Both implementation corpora read; source56aa30/5d6310 in first-combat-ai.md.
#include "combat_pose.hpp"

#include <cmath>
#include <stdexcept>

#include "bw/animation/secondary_motion.hpp"
namespace bw::game {
std::optional<math::Affine3<float>> original_attachment_pose(
    const formats::ControlPointDefinition* prepared,
    std::span<const math::Matrix4<float>> completed_skin,
    const math::Affine3<float>& model, std::int32_t attachment_slot) {
  if (!prepared || attachment_slot < 0 ||
      static_cast<std::size_t>(attachment_slot) >= prepared->frames().size()) {
    return std::nullopt;
  }
  const auto& source =
      prepared->frames().at(static_cast<std::size_t>(attachment_slot));
  if (!source.present) return std::nullopt;
  const auto frame = animation::evaluate_control_point(
      *prepared, source, completed_skin, math::homogeneous(model));
  constexpr math::Matrix4<float> half_turn{
      {{-1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, -1, 0}, {0, 0, 0, 1}}};
  return math::affine_part(math::multiply(half_turn, frame));
}
math::Affine3<float> original_weapon_attachment_pose(
    const formats::ControlPointDefinition* prepared,
    std::span<const math::Matrix4<float>> completed_skin,
    const math::Affine3<float>& model, std::int32_t attachment_slot) {
  return original_attachment_pose(prepared, completed_skin, model,
                                  attachment_slot)
      .value_or(model);
}
math::Vector3<float> original_actor_aim_point(
    const math::Vector3<float>& position, float completed_eye_height,
    std::optional<float> original_model_radius) {
  if (!math::all_finite(position) || !std::isfinite(completed_eye_height) ||
      (original_model_radius && !std::isfinite(*original_model_radius))) {
    throw std::invalid_argument("Nonfinite actor aim observation");
  }
  constexpr float half = .5F, minimum_eye_offset = .1F;
  auto result = position;
  const float offset = (completed_eye_height - position[1]) * half;
  if (offset >= minimum_eye_offset) {
    result[1] += offset;
  } else if (original_model_radius) {
    result[1] += *original_model_radius * half;
  }
  return result;
}
std::uint8_t original_npc_movement_category(
    const NpcMovementObservation& value) {
  if (!std::isfinite(value.actor_height) || !std::isfinite(value.eye_height) ||
      !std::isfinite(value.model_radius) || value.model_radius < 0) {
    throw std::invalid_argument("Invalid NPC movement observation");
  }
  if (value.squat_active) return 1;
  if (value.damage_motion) {
    constexpr std::uint32_t first = 757, last = 760;
    return *value.damage_motion >= first && *value.damage_motion <= last ? 1
                                                                         : 0;
  }
  constexpr float half = .5F, quarter = .25F;
  const auto eye_offset = value.eye_height - value.actor_height;
  if (eye_offset > value.model_radius * half) return 0;
  return eye_offset > value.model_radius * quarter ? 1 : 2;
}
}  // namespace bw::game
