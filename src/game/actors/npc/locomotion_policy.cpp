// Preserve the source destination -> limited heading -> motion nibble order.
// Shared math owns normalization/rotation; this file owns only game policy.
// Core E.4 (#re-design-invariants) and performance MEM.9 were reread for
// implementation: validate all observations before returning fixed copied
// state.
#include "locomotion_policy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "bw/math/euler.hpp"

namespace bw::game {
namespace {
constexpr std::uint32_t kDirectionMask = 15, kBack = 8, kForward = 9,
                        kLeft = 10, kRight = 11, kReadyIdle = 12,
                        kTurnLeft = 13, kTurnRight = 14;
constexpr float kRateSanityLimit = 1000;
constexpr float kFacingBias = .2617993950843811F;
constexpr double kSideAngle = .7853981852531433;
math::Vector3<float> unit(const math::Vector3<float>& value) {
  const auto length = math::length(value);
  if (!(length > 0) || !std::isfinite(length)) {
    throw std::invalid_argument("Invalid NPC locomotion direction");
  }
  return math::scale(value, 1 / length);
}
math::Vector3<float> rotate_y(const math::Vector3<float>& value, float angle) {
  return math::transform_direction<float>(
      value, math::euler_xyz_transform<float>({{0, angle, 0}, {}}).forward);
}
}  // namespace
NpcLocomotionSelection select_npc_locomotion(const NpcLocomotionInput& input) {
  if (!math::all_finite(input.position) ||
      !math::all_finite(input.destination) ||
      !math::all_finite(input.retained_heading) ||
      !math::all_finite(input.anchor_forward) ||
      !std::isfinite(input.playback_rate) ||
      !std::isfinite(input.turn_limit_radians) ||
      !std::isfinite(input.signed_turn_rate) ||
      !std::isfinite(input.world_delta_seconds) ||
      input.world_delta_seconds < 0 || (input.posture & kDirectionMask) != 0) {
    throw std::invalid_argument("Invalid NPC locomotion observation");
  }
  auto heading = unit(input.retained_heading);
  auto forward = input.anchor_forward;
  forward[1] = 0;
  forward = unit(forward);
  auto delta = math::subtract(input.destination, input.position);
  delta[1] = 0;
  const auto distance = math::length(delta);
  auto rate = input.stopped || distance < std::numeric_limits<float>::epsilon()
                  ? 0
                  : input.playback_rate;
  NpcLocomotionSelection result;
  result.retained_heading = heading;
  if (!input.stopped && distance > 0) {
    heading = math::scale(delta, 1 / distance);
    if (input.turn_limit_radians > 0) {
      const auto previous = unit(input.retained_heading);
      const auto angle = static_cast<float>(std::acos(static_cast<double>(
          std::clamp(math::dot(previous, heading), -1.F, 1.F))));
      const auto maximum = input.world_delta_seconds * input.turn_limit_radians;
      if (!std::isfinite(maximum)) {
        throw std::invalid_argument("NPC locomotion turn interval overflow");
      }
      if (maximum < angle) {
        const auto signed_angle =
            math::cross(previous, heading)[1] > 0 ? maximum : -maximum;
        heading = rotate_y(previous, signed_angle);
      }
      result.facing_request = heading;
      result.retained_heading = heading;
    }
  }
  if (std::abs(rate) > kRateSanityLimit) rate = 1;
  std::uint32_t direction = rate > 0 ? 0 : 1;
  if (rate > 0) {
    result.movement_direction = unit(heading);
    result.retained_heading = result.movement_direction;
    if (input.weapon_raised) {
      const auto reference = rotate_y(forward, kFacingBias);
      const auto alignment = math::dot(result.movement_direction, reference);
      const auto threshold = static_cast<float>(std::cos(kSideAngle));
      direction = alignment > threshold    ? kForward
                  : alignment < -threshold ? kBack
                  : math::cross(reference, result.movement_direction)[1] > 0
                      ? kRight
                      : kLeft;
    }
  } else {
    rate = 1;
    if (input.weapon_raised) {
      result.retained_heading = forward;
      direction = kReadyIdle;
      if (input.signed_turn_rate != 0) {
        direction = input.signed_turn_rate < 0 ? kTurnLeft : kTurnRight;
        rate = std::abs(input.signed_turn_rate);
      }
    }
  }
  result.motion.slot = input.posture | direction;
  result.motion.start = ActionMotionStart::preserve_phase;
  result.motion.rate = rate;
  return result;
}
}  // namespace bw::game
