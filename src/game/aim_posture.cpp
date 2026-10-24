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
constexpr float kTurnSpeed = 15.707963943481445F;
constexpr float kCollinear = .9990000128746033F;
constexpr float kOrdinaryLifetime = 6, kHeavyLifetime = .75F;
constexpr float kPostureFade = .25F;
constexpr std::uint32_t kHeavyWeapon = 3;
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
float raised_turn_input(float look_x, std::uint8_t sensitivity, bool inverted) {
  constexpr std::uint8_t kMaximumSetting = 127;
  constexpr float kSettingOffset = 25, kSettingScale = .019999999552965164F;
  if (!std::isfinite(look_x) || std::abs(look_x) > 1 ||
      sensitivity > kMaximumSetting) {
    throw std::invalid_argument("Invalid raised look input");
  }
  const float value = look_x *
                      (static_cast<float>(sensitivity) + kSettingOffset) *
                      kSettingScale;
  return inverted ? -value : value;
}
bool RaisedWeaponLifetime::advance(float seconds, bool upper_present,
                                   bool accepted_refresh,
                                   std::uint32_t weapon_class) {
  validate_seconds(seconds);
  if (weapon_class > kHeavyWeapon) {
    throw std::invalid_argument("Invalid weapon class");
  }
  if (seconds_ > 0 && !upper_present) {
    seconds_ = std::max(0.0F, seconds_ - seconds);
  }
  if (accepted_refresh) {
    seconds_ =
        weapon_class == kHeavyWeapon ? kHeavyLifetime : kOrdinaryLifetime;
  }
  return active();
}
float AimPostureBlend::advance(float seconds, bool enabled) {
  validate_seconds(seconds);
  const float step = seconds / kPostureFade;
  weight_ =
      enabled ? std::min(1.0F, weight_ + step) : std::max(0.0F, weight_ - step);
  return weight_;
}
Vec raised_facing_step(const RaisedFacingInput& input) {
  validate_seconds(input.seconds);
  if (!math::all_finite(input.current) || !(math::length(input.current) > 0)) {
    throw std::invalid_argument("Degenerate current facing");
  }
  validate_unit(input.requested);
  validate_unit(input.camera_forward);
  if (!math::all_finite(input.authored_reference)) {
    throw std::invalid_argument("Invalid facing reference");
  }
  const auto target = math::transform_direction<float>(
      input.requested, input.authored_reference);
  const float target_length = math::length(target);
  if (!math::all_finite(target) || !std::isfinite(target_length) ||
      !(target_length > 0)) {
    throw std::invalid_argument("Degenerate facing target");
  }
  const Vec horizontal{input.camera_forward[0], 0, input.camera_forward[2]};
  if (math::length(horizontal) == 0) {
    throw std::invalid_argument("Vertical facing camera");
  }
  const auto camera = math::divide(horizontal, math::length(horizontal));
  const float angle = input.seconds * kTurnSpeed;
  const float alignment = math::dot(input.current, target);
  // The source compares raw authored directions, including root scale. Its
  // final basis construction normalizes the chosen forward only afterward.
  if (alignment >= std::cos(angle)) {
    return math::divide(target, target_length);
  }
  Vec axis{};
  if (alignment >= -kCollinear) {
    const auto crossed = math::cross(input.current, target);
    axis = math::divide(crossed, math::length(crossed));
    const auto negative_camera = math::scale(camera, -1.0F);
    if (input.posture_enabled &&
        crossed[1] * math::cross(input.current, negative_camera)[1] >= 0 &&
        -crossed[1] * math::cross(target, negative_camera)[1] >= 0) {
      axis = math::scale(axis, -1.0F);
    }
  } else if (std::abs(math::dot(camera, input.current)) <= kCollinear) {
    const auto crossed = math::cross(input.current, camera);
    axis = math::divide(crossed, math::length(crossed));
  } else {
    axis = {0, -1, 0};
  }
  const float half = angle / 2;
  const auto vector = math::scale(axis, std::sin(half));
  const auto rotation =
      math::homogeneous_transform(math::TransformComponents<float>{
          {vector[0], vector[1], vector[2], std::cos(half)}, {}, {1, 1, 1}});
  const auto result = math::transform_direction<float>(input.current, rotation);
  const float result_length = math::length(result);
  if (!math::all_finite(result) || !std::isfinite(result_length) ||
      !(result_length > 0)) {
    throw std::invalid_argument("Unrepresentable facing step");
  }
  return math::divide(result, result_length);
}
}  // namespace bw::game
