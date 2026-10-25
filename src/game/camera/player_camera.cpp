// Context changes retain the source outgoing/live-camera transition, while
// shared math owns interpolation and quaternion computation. Both-phase I.1
// separates camera context from raised animation; MEM.9 keeps fixed snapshots.
// Source curve/frustum/collision details: player-camera-targeting.md.
// Shared math retains each caller's scalar/accumulator precision; tolerance
// and failure policy remain here. Both-phase I.9/MEM.9 contracts are in
// bw/math/vector.hpp and matrix.hpp.
// Advance independent look state, restore collision-shortened offsets and
// compose the three ordered clearance stages. Work on a fixed-size candidate
// before publication so malformed input/query failure preserves the old view.
// Header contracts own lifetime/threading; player-camera-follow.md
// distinguishes recovered source from the hard angular limit reconstruction.
// Both MCP phases: I.1 (ri-explicit) exposes normalized input and elapsed-time
// contracts; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps
// all state fixed.
#include "player_camera.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "bw/math/matrix.hpp"
#include "bw/math/quaternion.hpp"
#include "bw/math/scalar.hpp"
#include "bw/math/vector.hpp"

namespace bw::game {
namespace {
using Vec = math::Vector3<float>;
constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
constexpr float kFullTurn = 2 * std::numbers::pi_v<float>;
constexpr float kFullTurnDegrees = 360;
constexpr float kOriginalAspect = 16.0F / 9.0F;
constexpr float kLookThreshold = .05F;
constexpr float kClearance = .05F;
constexpr double kContextSeconds = .25;
constexpr float kContextShape = 4;
// Portable DirectXMath shortest-path near-parallel policy; see research.
constexpr float kSlerpLinearThreshold = 0.9999899864F;
constexpr std::uint32_t kExcludedFlags = 0x2000;
void validate_pose(const ScriptedPlayerPose& player) {
  if (!math::all_finite(player.position) ||
      !std::isfinite(player.yaw_degrees)) {
    throw std::invalid_argument("Nonfinite player camera anchor");
  }
}
Vec offset(const Vec& origin, float distance, const Vec& direction) {
  const auto result = math::add(origin, math::scale(direction, distance));
  if (!math::all_finite(result)) {
    throw std::invalid_argument("Unrepresentable player camera offset");
  }
  return result;
}
float projected_offset(const Vec& point, const Vec& origin, const Vec& axis) {
  const auto result =
      math::accumulated_dot(math::subtract(point, origin), axis);
  if (!std::isfinite(result)) {
    throw std::invalid_argument("Unrepresentable player camera clearance");
  }
  return result;
}
}  // namespace
PlayerCamera::PlayerCamera(const formats::PlayerCameraSettings& settings,
                           const ScriptedPlayerPose& player,
                           const scene::StaticCollision& collision)
    : settings_(settings),
      collision_(&collision),
      preset_(settings.contexts.front()) {
  if (!std::isfinite(settings_.height) || !math::all_finite(settings_.offset) ||
      !std::isfinite(preset_.pitch_coefficients[0]) ||
      !std::isfinite(preset_.pitch_coefficients[1]) ||
      !std::isfinite(settings_.transition_speed) ||
      settings_.transition_speed < 0 ||
      !std::isfinite(settings_.look_radians_per_second) ||
      settings_.look_radians_per_second <= 0 ||
      !std::isfinite(settings_.pitch_limit_radians) ||
      settings_.pitch_limit_radians <= 0 ||
      settings_.pitch_limit_radians >= std::numbers::pi_v<float> / 2) {
    throw std::invalid_argument("Invalid player camera settings");
  }
  for (const auto& preset : settings_.contexts) {
    if (!std::isfinite(preset.height) || !math::all_finite(preset.offset) ||
        !math::all_finite(preset.pitch_coefficients) ||
        !std::isfinite(preset.vertical_fov_radians) ||
        preset.vertical_fov_radians <= 0 ||
        preset.vertical_fov_radians >= std::numbers::pi_v<float> ||
        !math::all_finite(preset.look_radians_per_second) ||
        preset.look_radians_per_second[0] <= 0 ||
        preset.look_radians_per_second[1] <= 0) {
      throw std::invalid_argument("Invalid player camera context");
    }
  }
  view_.perspective = {settings_.vertical_fov_radians, kOriginalAspect,
                       settings_.near_plane, settings_.far_plane};
  teleport(player);
  // Initial and context frusta are validated before the first publication.
  (void)render::make_view_transforms(view_.pose, view_.perspective);
}
void PlayerCamera::set_far_distance(float distance) {
  if (!std::isfinite(distance) || distance <= view_.perspective.near_plane) {
    throw std::invalid_argument("Invalid region camera far distance");
  }
  view_.perspective.far_plane = distance;
}
void PlayerCamera::set_context(PlayerCameraContext context, bool crouched) {
  if (context != PlayerCameraContext::ordinary &&
      context != PlayerCameraContext::targeting) {
    throw std::invalid_argument("Invalid player camera context selector");
  }
  if (context == context_ && crouched == crouched_) return;
  transition_from_ = view_;
  transition_rotation_ = rotation_;
  transition_elapsed_ = 0;
  transition_duration_ = kContextSeconds;
  context_ = context;
  crouched_ = crouched;
  const std::size_t index =
      (crouched ? 2U : 0U) +
      (context == PlayerCameraContext::targeting ? 1U : 0U);
  preset_ = settings_.contexts.at(index);
  // The live incoming camera snaps its preset; the outgoing snapshot owns
  // visual continuity, exactly as the separate original camera transition.
  height_ = preset_.height;
  offset_ = preset_.offset;
}
void PlayerCamera::teleport(const ScriptedPlayerPose& player) {
  validate_pose(player);
  auto candidate = *this;
  candidate.yaw_ =
      std::remainder(player.yaw_degrees, kFullTurnDegrees) * kRadians;
  candidate.pitch_ = 0;
  candidate.height_ = preset_.height;
  candidate.offset_ = preset_.offset;
  candidate.transition_duration_ = 0;
  candidate.transition_elapsed_ = 0;
  candidate.resolve(player.position);
  *this = candidate;
}
const InitialPlayerCamera& PlayerCamera::advance(
    const ScriptedPlayerPose& player, PlayerCameraLook look,
    double delta_seconds) {
  validate_pose(player);
  const double recovery = delta_seconds * settings_.transition_speed;
  const double horizontal = delta_seconds * preset_.look_radians_per_second[0];
  const double vertical_rotation =
      delta_seconds * preset_.look_radians_per_second[1];
  if (!std::isfinite(delta_seconds) || delta_seconds < 0 ||
      !std::isfinite(look.right) || !std::isfinite(look.up) ||
      std::abs(look.right) > 1 || std::abs(look.up) > 1 ||
      !std::isfinite(recovery) || !std::isfinite(horizontal) ||
      !std::isfinite(vertical_rotation) ||
      recovery > std::numeric_limits<float>::max()) {
    throw std::invalid_argument("Invalid player camera advancement");
  }
  auto candidate = *this;
  // Look is retained independently of the actor's locomotion-facing yaw.
  if (std::abs(look.right) >= kLookThreshold ||
      std::abs(look.up) >= kLookThreshold) {
    candidate.yaw_ = static_cast<float>(std::remainder(
        static_cast<double>(yaw_) + horizontal * look.right, kFullTurn));
    candidate.pitch_ = static_cast<float>(
        std::clamp(static_cast<double>(pitch_) + vertical_rotation * look.up,
                   -static_cast<double>(settings_.pitch_limit_radians),
                   static_cast<double>(settings_.pitch_limit_radians)));
  }
  candidate.height_ = static_cast<float>(
      std::min(static_cast<double>(preset_.height), height_ + recovery));
  const double length =
      std::hypot(preset_.offset[0], preset_.offset[1], preset_.offset[2]);
  if (length > 0) {
    for (std::size_t i = 0; i < offset_.size(); ++i) {
      const auto desired = preset_.offset.at(i);
      const auto advanced = offset_.at(i) + desired / length * recovery;
      candidate.offset_.at(i) = std::copysign(
          static_cast<float>(std::min(std::abs(advanced),
                                      static_cast<double>(std::abs(desired)))),
          desired);
    }
  }
  candidate.resolve(player.position);
  candidate.blend_transition(delta_seconds, view_.pose.eye);
  *this = candidate;
  return view_;
}
void PlayerCamera::blend_transition(double seconds,
                                    const std::array<float, 3>& previous_eye) {
  if (transition_duration_ <= 0) return;
  transition_elapsed_ += seconds;
  if (transition_elapsed_ >= transition_duration_) {
    transition_duration_ = 0;
    return;
  }
  const auto t = static_cast<float>(transition_elapsed_ / transition_duration_);
  const float weight = kContextShape * t / ((1 - t) + kContextShape * t);
  const auto target_eye = view_.pose.eye;
  rotation_ = math::quaternion_slerp(
      transition_rotation_, rotation_,
      math::SphericalBlend<float>{weight, kSlerpLinearThreshold});
  const auto basis = math::affine_transform<float>({rotation_, {}, {1, 1, 1}});
  auto& pose = view_.pose;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    pose.right.at(axis) = basis[0].at(axis);
    pose.up.at(axis) = basis[1].at(axis);
    pose.forward.at(axis) = basis[2].at(axis);
  }
  pose.eye = math::add(math::scale(transition_from_.pose.eye, 1 - weight),
                       math::scale(target_eye, weight));
  const auto from =
      std::tan(transition_from_.perspective.vertical_fov_radians / 2);
  const auto to = std::tan(preset_.vertical_fov_radians / 2);
  view_.perspective.vertical_fov_radians =
      2 * std::atan(math::linear_interpolate(from, to, weight));
  // Original transition first sweeps from its displayed eye, then from the
  // live target camera. A corrected position restarts the remaining duration.
  const auto swept = collision_->clip_segment(
      {previous_eye, pose.eye, kClearance, kExcludedFlags});
  if (swept.clipped) {
    pose.eye = collision_
                   ->clip_segment(
                       {target_eye, swept.position, kClearance, kExcludedFlags})
                   .position;
    transition_from_.pose = pose;
    transition_rotation_ = rotation_;
    transition_duration_ -= transition_elapsed_;
    transition_elapsed_ = 0;
  }
}
void PlayerCamera::resolve(const std::array<float, 3>& position) {
  const float sine_yaw = std::sin(yaw_), cosine_yaw = std::cos(yaw_);
  const float sine_pitch = std::sin(pitch_), cosine_pitch = std::cos(pitch_);
  view_.perspective.vertical_fov_radians = preset_.vertical_fov_radians;
  const math::Vector<float, 4> yaw_rotation{0, std::sin(yaw_ / 2), 0,
                                            std::cos(yaw_ / 2)};
  const math::Vector<float, 4> pitch_rotation{-std::sin(pitch_ / 2), 0, 0,
                                              std::cos(pitch_ / 2)};
  rotation_ = math::quaternion_product<float>({yaw_rotation, pitch_rotation});
  auto& pose = view_.pose;
  pose.right = {cosine_yaw, 0, -sine_yaw};
  pose.up = {-sine_yaw * sine_pitch, cosine_pitch, -cosine_yaw * sine_pitch};
  pose.forward = {sine_yaw * cosine_pitch, sine_pitch,
                  cosine_yaw * cosine_pitch};
  // Resolve world-vertical anchoring before camera-local shoulder and rear.
  const auto height =
      collision_->clip_segment({position, offset(position, height_, {0, 1, 0}),
                                kClearance, kExcludedFlags});
  if (height.clipped) {
    height_ = height.position[1] - position[1];
  }
  const float coefficient = pose.forward[1] < 0
                                ? preset_.pitch_coefficients[0]
                                : -preset_.pitch_coefficients[1];
  const float vertical =
      offset_[1] + pose.forward[1] * pose.forward[1] * coefficient;
  const auto shoulder = collision_->clip_segment(
      {height.position,
       offset(offset(height.position, offset_[0], pose.right), vertical,
              pose.up),
       kClearance, kExcludedFlags});
  if (shoulder.clipped) {
    offset_[0] =
        projected_offset(shoulder.position, height.position, pose.right);
    offset_[1] = projected_offset(shoulder.position, height.position, pose.up);
  }
  const auto rear = collision_->clip_segment(
      {shoulder.position, offset(shoulder.position, offset_[2], pose.forward),
       kClearance, kExcludedFlags});
  if (rear.clipped) {
    offset_[2] =
        projected_offset(rear.position, shoulder.position, pose.forward);
  }
  pose.eye = rear.position;
}
}  // namespace bw::game
