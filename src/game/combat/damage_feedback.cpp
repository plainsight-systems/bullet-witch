// Source ordinary-hit direction policy (6874d0): negate projectile travel,
// express its horizontal component in the current horizontal camera basis,
// normalize, then compare front/rear before right/left. This is feedback, not
// damage eligibility. Core I.1 and MEM.9 keep pure copied fixed-size
// arithmetic.
#include "damage_feedback.hpp"

#include <cmath>
#include <stdexcept>
namespace bw::game {
DamageDirection classify_damage_direction(
    const math::Vector3<float>& incoming,
    const math::Vector3<float>& camera_forward) {
  const math::Vector3<float> horizontal{camera_forward[0], 0,
                                        camera_forward[2]};
  if (!math::all_finite(incoming) || !math::all_finite(camera_forward) ||
      !(math::dot(horizontal, horizontal) > 0)) {
    throw std::invalid_argument("Invalid damage feedback direction");
  }
  const auto forward = math::divide(horizontal, math::length(horizontal));
  const auto right = math::cross(math::Vector3<float>{0, 1, 0}, forward);
  const auto toward = math::scale(incoming, -1.F);
  math::Vector3<float> local{math::dot(toward, right), 0,
                             math::dot(toward, forward)};
  const float squared = math::dot(local, local);
  if (squared > 0) local = math::scale(local, 1.F / std::sqrt(squared));
  constexpr double kSourceSectorAngle = .7853981852531433;
  const float boundary = static_cast<float>(std::cos(kSourceSectorAngle));
  if (local[2] > boundary) return DamageDirection::front;
  if (local[2] < -boundary) return DamageDirection::rear;
  return local[0] > boundary ? DamageDirection::right : DamageDirection::left;
}
}  // namespace bw::game
