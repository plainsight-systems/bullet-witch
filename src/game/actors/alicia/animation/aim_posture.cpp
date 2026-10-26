// Source target spaces, body pitch limits and update ordering stay here;
// vector/matrix/quaternion arithmetic is shared engine math. Validate the
// whole request before mutating retained scalar state (E.4); MEM.9 fixed work.
#include "aim_posture.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace bw::game {
namespace {
using Vec = math::Vector3<float>;
constexpr Vec kUp{0, 1, 0};
constexpr float kUnitTolerance = .001F;
constexpr float kPitchClampCeiling = 89.9000015258789F;
constexpr float kPostureFade = .25F;
void validate_seconds(float value) {
  if (!std::isfinite(value) || value < 0) {
    throw std::invalid_argument("Invalid aim timestep");
  }
}
void validate_unit(const Vec& value) {
  if (!math::all_finite(value) ||
      std::abs(math::dot(value, value) - 1) > kUnitTolerance) {
    throw std::invalid_argument("Nonunit aim direction");
  }
}
}  // namespace
Vec aim_posture_target(const AimPostureTarget& input) {
  validate_unit(input.retained_direction);
  if (!math::all_finite(input.anchor) || !math::all_finite(input.model) ||
      !math::all_finite(input.pitch_degrees)) {
    throw std::invalid_argument("Nonfinite aim target settings");
  }
  auto anchor = input.anchor;
  Vec forward{anchor[2][0], anchor[2][1], anchor[2][2]};
  validate_unit(forward);
  if (input.camera_present) {
    const std::size_t index =
        static_cast<std::size_t>(forward[1] < 0) + (input.crouched ? 2 : 0);
    const float limit = input.pitch_degrees.at(index);
    if (limit <= kPitchClampCeiling) {
      const float sine = std::sin((limit * std::numbers::pi_v<float>) / 180.0F);
      if (std::abs(sine) < std::abs(forward[1])) {
        const Vec horizontal{forward[0], 0, forward[2]};
        const float length = math::length(horizontal);
        if (length == 0) {
          throw std::invalid_argument("Vertical aim cannot apply pitch clamp");
        }
        forward = math::scale(math::divide(horizontal, length),
                              std::sqrt(1 - sine * sine));
        forward[1] = sine;
      }
      if (math::dot(math::cross(kUp, forward), math::cross(kUp, forward)) ==
          0) {
        throw std::invalid_argument("Vertical aim basis");
      }
      const auto translation = math::translation(anchor);
      anchor = math::orthonormal_basis(forward, kUp);
      math::set_translation(anchor, translation);
    }
  }
  const auto inverse = math::inverse_affine(input.model);
  if (!inverse) throw std::invalid_argument("Singular aim model basis");
  auto result =
      math::transform_direction<float>(input.retained_direction, anchor);
  result = math::transform_direction<float>(result, *inverse);
  const float length = math::length(result);
  if (!(length > 0)) throw std::invalid_argument("Degenerate model aim target");
  result = math::divide(result, length);
  result[1] = -result[1];
  return result;
}
float AimPostureBlend::advance(float seconds, bool enabled) {
  validate_seconds(seconds);
  const float step = seconds / kPostureFade;
  weight_ =
      enabled ? std::min(1.0F, weight_ + step) : std::max(0.0F, weight_ - step);
  return weight_;
}
}  // namespace bw::game
