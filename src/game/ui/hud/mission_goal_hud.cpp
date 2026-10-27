// Math boundary: both-phase Core F.4 / performance MEM.9 keep generic
// fixed-value operations in bw::math; source policy remains with this owner.
// Project the retained script target, advance the original receiver phases,
// then emit its rotating marker/distance or bounded directional arrow. Shared
// camera/vector math owns coordinate arithmetic; this owner supplies UI policy.
// F.16 and MEM.9 were reread for implementation: observations are const
// borrows, all commands/state remain inline, and only admitted world intervals
// advance.
#include "mission_goal_hud.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "bw/math/matrix.hpp"
#include "bw/math/scalar.hpp"

namespace bw::game {
namespace {
constexpr float kDistanceBias = .01F;
constexpr float kFadeTolerance = .01F, kFadeRate = 6;
constexpr float kCenterX = 640, kCenterY = 360;
constexpr float kVisibleInset = 32, kVisibleRight = 1248, kVisibleBottom = 688;
constexpr float kArrowRadiusX = 576, kArrowRadiusY = 324,
                kArrowExpansion = 1.181F;
constexpr float kArrowLeft = 128, kArrowRight = 1152, kArrowTop = 72,
                kArrowBottom = 648;
constexpr unsigned kCounterClockwiseImage = 25, kClockwiseImage = 26,
                   kPointImage = 27;
constexpr unsigned kArrowImage = 28, kDistanceImage = 432, kDigitsImage = 433;
constexpr float kMarkerLeft = 32, kMarkerTop = 96, kPointLeft = 16,
                kPointTop = 44;
constexpr float kDistanceTop = 148, kDigitsRight = 95, kDigitsTop = 137,
                kDigitAdvance = 14;
constexpr unsigned kDigitWidth = 24, kDigitHeight = 40, kDigitRadix = 10;
}  // namespace
void MissionGoalHud::set_target(std::array<float, 3> position) {
  if (!math::all_finite(position)) {
    throw std::invalid_argument("Invalid mission goal position");
  }
  if (!supplied_ || phase_ == Phase::hidden) {
    phase_ = Phase::initial;
  }
  supplied_ = true;
  target_ = position;
}
void MissionGoalHud::reset() noexcept { *this = MissionGoalHud{}; }
std::span<const HudSprite> MissionGoalHud::commands() const noexcept {
  return {commands_.data(), count_};
}
void MissionGoalHud::advance(const MissionGoalObservation& observed,
                             float seconds) {
  if (!std::isfinite(seconds) || seconds < 0 ||
      !math::all_finite(observed.player_position)) {
    throw std::invalid_argument("Invalid mission goal observation");
  }
  if (phase_ == Phase::hidden) {
    count_ = 0;
    return;
  }
  const auto view =
      render::make_view_transforms(observed.camera, observed.perspective);
  const std::array<float, 4> point{target_[0], target_[1], target_[2], 1};
  const auto clip = math::multiply(view.clip_rows, point);
  const auto separation = math::subtract(target_, observed.player_position);
  // Original target has w=1 and player observation w=0; preserve that fourth
  // component in its distance display rather than silently changing the norm.
  const double distance =
      std::sqrt(static_cast<double>(math::dot(separation, separation)) + 1);
  if (!std::isfinite(distance) ||
      distance + kDistanceBias >=
          static_cast<double>(std::numeric_limits<int>::max())) {
    throw std::invalid_argument("Mission goal distance exceeds source range");
  }
  float opacity_target = 1;
  switch (phase_) {
    case Phase::hidden:
      break;
    case Phase::initial:
      opacity_ = 0;
      phase_ = Phase::begin;
      break;
    case Phase::begin:
      phase_ = Phase::live;
      break;
    case Phase::live:
      if (!supplied_) phase_ = Phase::fade;
      break;
    case Phase::fade:
      opacity_target = 0;
      phase_ = Phase::fading;
      break;
    case Phase::fading:
      opacity_target = 0;
      if (opacity_ < kFadeTolerance) phase_ = Phase::retire;
      break;
    case Phase::retire:
      phase_ = Phase::hidden;
      count_ = 0;
      return;
  }
  opacity_ = math::linear_interpolate(opacity_, opacity_target,
                                      std::min(kFadeRate * seconds, 1.F));
  constexpr float kTau = 2 * std::numbers::pi_v<float>;
  const float rotation = std::fmod(rotation_elapsed_, 3.F) / 3.F * kTau;
  const float pulse = std::fmod(pulse_elapsed_, 2.F) * .5F;
  rotation_elapsed_ += seconds;
  pulse_elapsed_ += seconds;
  const float opacity = std::floor(opacity_ * 255) / 255;
  count_ = 0;
  const float x = clip[3] == 0 ? 0 : (clip[0] / clip[3] + 1) * kCenterX;
  const float y = clip[3] == 0 ? 0 : (1 - clip[1] / clip[3]) * kCenterY;
  const float z = clip[3] == 0 ? -1 : clip[2] / clip[3];
  if (z < 0 || z >= 1 || x <= kVisibleInset || x >= kVisibleRight ||
      y <= kVisibleInset || y >= kVisibleBottom) {
    // The source adds the planar bearing of inverse-camera row2. In our
    // explicit rigid basis that row is (right.z, up.z, forward.z); retain its
    // pitch/roll behavior rather than replacing it with a yaw subtraction.
    const auto toward = math::subtract(target_, observed.camera.eye);
    const float bearing =
        std::atan2(toward[0], toward[2]) +
        std::atan2(observed.camera.right[2], observed.camera.forward[2]);
    const float curve = 4 * pulse * (1 - pulse);
    const float alpha = std::clamp(opacity + 1.1F * curve - .6F, 0.F, 1.F);
    commands_.at(count_++) = {
        .image = kArrowImage,
        .destination = {std::clamp(kCenterX + std::sin(bearing) *
                                                  kArrowRadiusX *
                                                  kArrowExpansion,
                                   kArrowLeft, kArrowRight),
                        std::clamp(kCenterY - std::cos(bearing) *
                                                  kArrowRadiusY *
                                                  kArrowExpansion,
                                   kArrowTop, kArrowBottom)},
        .color = {1, 1, 1, alpha},
        .rotation_radians = bearing,
        .centered = true};
    return;
  }
  const render::Color color{1, 1, 1, opacity};
  commands_.at(count_++) = {.image = kCounterClockwiseImage,
                            .destination = {x - kMarkerLeft, y - kMarkerTop},
                            .color = color,
                            .rotation_radians = -rotation};
  commands_.at(count_++) = {.image = kClockwiseImage,
                            .destination = {x - kMarkerLeft, y - kMarkerTop},
                            .color = color,
                            .rotation_radians = rotation};
  commands_.at(count_++) = {.image = kPointImage,
                            .destination = {x - kPointLeft, y - kPointTop},
                            .color = color};
  commands_.at(count_++) = {.image = kDistanceImage,
                            .destination = {x, y - kDistanceTop},
                            .color = color};
  auto value = static_cast<unsigned>(distance + kDistanceBias);
  float digit_x = x + kDigitsRight;
  for (;;) {
    digit_x -= kDigitAdvance;
    commands_.at(count_++) = {
        .image = kDigitsImage,
        .destination = {digit_x, y - kDigitsTop, kDigitWidth, kDigitHeight},
        .color = color,
        .tile = HudAtlasTile{value % kDigitRadix, kDigitWidth, kDigitHeight}};
    value /= kDigitRadix;
    if (value == 0) break;
  }
}
}  // namespace bw::game
