// Alicia raised-weapon action lifetime and facing policy. PlayerController owns
// the scalar lifetime; calls consume validated simulation seconds and accepted
// input in source update order. Invalid inputs throw before state mutation.
// No allocation; constant work. Numerical policy is preserved from the original
// raised-aim-posture evidence, independent of pose blending and target
// transforms. Both-phase Core I.1 (#ri-explicit), SF.11 (#rs-contained) keep
// action/pose ownership explicit; performance COPY.7 (eliminate-hidden-copies)
// retains small value inputs without copying actor/catalog state.
#include "raised_weapon_state.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bw::game {
namespace {
using Vec = math::Vector3<float>;
constexpr float kUnitTolerance = .001F;
constexpr float kTurnSpeed = 15.707963943481445F;
constexpr float kCollinear = .9990000128746033F;
constexpr float kOrdinaryLifetime = 6, kHeavyLifetime = .75F;
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
