// The pose's directions, fields of view and blend (bw/camera/pose.hpp).
#include "bw/camera/pose.hpp"

#include "bw/math/rotation/quaternion.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/world/object.hpp"

#include <cmath>
#include <numbers>

namespace bw::camera {
namespace {

constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0F;
constexpr float kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
float lerp(float from, float to, float factor) noexcept { return from + factor * (to - from); }

// to - from brought into (-180, 180].
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
float short_difference(float from, float to) noexcept {
  float difference = std::fmod(to - from, 360.0F);
  if (difference > 180.0F) {
    difference -= 360.0F;
  } else if (difference <= -180.0F) {
    difference += 360.0F;
  }
  return difference;
}

} // namespace

// The view's aspect is the sensor's, and the vertical half field is
// atan(tan(half the horizontal) / that aspect).
float vertical_fov_degrees(const Pose& pose) noexcept {
  const float half = pose.field_of_view_degrees * 0.5F * kDegreesToRadians;
  const float aspect = pose.sensor_width / pose.sensor_height;
  return 2.0F * std::atan(std::tan(half) / aspect) * kRadiansToDegrees;
}

// The signature is the header's: a vertical field and an aspect.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
float horizontal_fov_degrees(float vertical_degrees, float aspect) noexcept {
  const float half = vertical_degrees * 0.5F * kDegreesToRadians;
  return 2.0F * std::atan(std::tan(half) * aspect) * kRadiansToDegrees;
}

// The signature is the header's: yaw then pitch.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
math::Vector3f forward(float yaw_degrees, float pitch_degrees) noexcept {
  const float yaw = yaw_degrees * kDegreesToRadians;
  const float pitch = pitch_degrees * kDegreesToRadians;
  return math::Vector3f{std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                        -std::cos(yaw) * std::cos(pitch)};
}

math::Vector3f right(float yaw_degrees) noexcept {
  const float yaw = yaw_degrees * kDegreesToRadians;
  return math::Vector3f{std::cos(yaw), 0.0F, std::sin(yaw)};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
math::Vector3f up(float yaw_degrees, float pitch_degrees) noexcept {
  return right(yaw_degrees).cross(forward(yaw_degrees, pitch_degrees));
}

// The yaw of forward(yaw, 0) that points where `rotation` turns an
// object's forward (-z): sin(yaw) = d.x and -cos(yaw) = d.z.
float facing_yaw_degrees(const math::Quaternion4f& rotation) noexcept {
  const math::Vector3f direction = rotation.rotate(
      math::Vector3f{world::kObjectForwardX, world::kObjectForwardY, world::kObjectForwardZ});
  if (direction.x == 0.0F && direction.z == 0.0F) {
    return 0.0F;
  }
  float yaw = std::atan2(direction.x, -direction.z) * kRadiansToDegrees;
  if (yaw < 0.0F) {
    yaw += 360.0F;
  }
  return yaw >= 360.0F ? 0.0F : yaw;
}

// Factor 0 changes nothing and factor 1 overrides; otherwise each number is
// interpolated, and a rotation adds the factor times the short difference.
Pose blend(const Pose& from_pose, const Pose& to_pose, float factor) noexcept {
  if (factor <= 0.0F) {
    return from_pose;
  }
  if (factor >= 1.0F) {
    return to_pose;
  }
  Pose pose{};
  pose.location = math::Vector3f{lerp(from_pose.location.x, to_pose.location.x, factor),
                                 lerp(from_pose.location.y, to_pose.location.y, factor),
                                 lerp(from_pose.location.z, to_pose.location.z, factor)};
  pose.yaw_degrees =
      from_pose.yaw_degrees + factor * short_difference(from_pose.yaw_degrees, to_pose.yaw_degrees);
  pose.pitch_degrees = from_pose.pitch_degrees +
                       factor * short_difference(from_pose.pitch_degrees, to_pose.pitch_degrees);
  pose.field_of_view_degrees =
      lerp(from_pose.field_of_view_degrees, to_pose.field_of_view_degrees, factor);
  pose.sensor_width = lerp(from_pose.sensor_width, to_pose.sensor_width, factor);
  pose.sensor_height = lerp(from_pose.sensor_height, to_pose.sensor_height, factor);
  pose.near_plane = lerp(from_pose.near_plane, to_pose.near_plane, factor);
  pose.far_plane = lerp(from_pose.far_plane, to_pose.far_plane, factor);
  return pose;
}

} // namespace bw::camera
