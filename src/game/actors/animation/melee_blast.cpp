// Source melee blast phase policy. Shared math performs inverse transforms and
// angular stepping; this owner retains only source mode/heading/vertical speed.
// Implementation Core I.4 and MEM.9 (retrieved again) keep copied frame facts
// separate from mutable motion, contact and provider owners.
#include "melee_blast.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/math/direction.hpp"
#include "bw/math/euler.hpp"
namespace bw::game {
namespace {
constexpr std::array<std::uint32_t, 4> kStart{844, 841, 847, 850};
constexpr std::array<std::uint32_t, 4> kFlight{843, 840, 846, 849};
constexpr std::array<std::uint32_t, 4> kLanding{842, 839, 845, 848};
constexpr float kMinimumSquared = 1.0000001111620804e-6F;
constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2;
constexpr float kNearAngle = .7853981852531433F;
constexpr float kFarAngle = 2.356194496154785F;
constexpr float kTurnRate = 25.132741928100586F;
constexpr float kGravity = 9.800000190734863F;
constexpr double kBlend = .3, kLandingBlend = .1;
constexpr math::DirectionTurnPolicy<float> kTurn{
    {0, 1, 0}, .9990000128746033F, kMinimumSquared};
math::Vector3<float> rotate_y(math::Vector3<float> value, float radians) {
  return math::transform_direction<float>(
      value, math::euler_xyz_transform<float>({{0, radians, 0}, {}}).forward);
}
void valid_delta(float seconds) {
  if (!std::isfinite(seconds) || seconds <= 0) {
    throw std::invalid_argument("Melee blast requires a positive world delta");
  }
}
}  // namespace
ActionMotionRequest MeleeBlast::begin(math::Vector3<float> incoming,
                                      const math::Matrix4<float>& model) {
  if (!math::all_finite(incoming)) {
    throw std::invalid_argument("Invalid melee blast direction");
  }
  const auto inverse = math::inverse_affine(model);
  if (!inverse) throw std::invalid_argument("Invalid melee blast model basis");
  const auto affine = math::affine_part(model);
  incoming[1] = 0;
  const auto squared = math::dot(incoming, incoming);
  heading_ = squared < kMinimumSquared
                 ? affine[2]
                 : math::scale(incoming, -1.F / std::sqrt(squared));
  // Native68b060 receives this adjusted direction as its point argument: it
  // subtracts model translation before transforming horizontal coordinates.
  // Preserve that caller contract, including its unusual position dependence.
  auto offset = math::subtract(heading_, affine[3]);
  offset[1] = 0;
  const auto relative = math::transform_direction<float>(offset, *inverse);
  const auto angle = math::dot(relative, relative) < kMinimumSquared
                         ? 0.F
                         : std::atan2(relative[0], relative[2]);
  direction_ = 0;
  if (angle < -kFarAngle || angle > kFarAngle) {
    direction_ = 1;
    heading_ = math::scale(heading_, -1.F);
  } else if (angle >= -kFarAngle && angle <= -kNearAngle) {
    direction_ = 2;
    heading_ = rotate_y(heading_, kQuarterTurn);
  } else if (angle >= kNearAngle && angle <= kFarAngle) {
    direction_ = 3;
    heading_ = rotate_y(heading_, -kQuarterTurn);
  }
  vertical_velocity_ = 0;
  ballistic_ = false;
  phase_ = MeleeBlastPhase::start;
  return {
      .slot = kStart.at(direction_), .blend_seconds = kBlend, .fresh = true};
}
math::Vector3<float> MeleeBlast::facing(math::Vector3<float> current,
                                        float seconds) const {
  valid_delta(seconds);
  if (phase_ != MeleeBlastPhase::start) return current;
  return math::turn_toward(
      current, heading_,
      std::min(kTurnRate * seconds, std::numbers::pi_v<float>), kTurn);
}
math::Vector3<float> MeleeBlast::displacement(math::Vector3<float> root,
                                              float seconds, bool completed) {
  valid_delta(seconds);
  if (!math::all_finite(root)) {
    throw std::invalid_argument("Invalid melee blast root displacement");
  }
  if (phase_ != MeleeBlastPhase::flight) return root;
  if (!ballistic_) {
    vertical_velocity_ = root[1] / seconds;
    ballistic_ = completed;
  } else {
    const float previous = vertical_velocity_;
    vertical_velocity_ -= kGravity * seconds;
    root[1] += (previous + vertical_velocity_) * seconds / 2;
  }
  return root;
}
MeleeBlastTransition MeleeBlast::finish(const MeleeBlastStep& step) {
  if (phase_ == MeleeBlastPhase::start && step.motion_completed) {
    phase_ = MeleeBlastPhase::flight;
    return {{ActionMotionRequest{.slot = kFlight.at(direction_),
                                 .blend_seconds = kBlend,
                                 .fresh = true}}};
  }
  if (phase_ == MeleeBlastPhase::flight && step.grounded) {
    phase_ = MeleeBlastPhase::landing;
    return {{ActionMotionRequest{.slot = kLanding.at(direction_),
                                 .blend_seconds = kLandingBlend,
                                 .fresh = true}},
            true};
  }
  if (phase_ == MeleeBlastPhase::landing && step.crossed_death_frame) {
    phase_ = MeleeBlastPhase::inactive;
    return {.physical_handoff = true};
  }
  return {};
}
}  // namespace bw::game
