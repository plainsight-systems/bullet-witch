#ifndef BW_CAMERA_POSE_HPP
#define BW_CAMERA_POSE_HPP

#include "bw/math/rotation/quaternion.hpp"
#include "bw/math/vector/vector3.hpp"

// Responsibility: where the camera is and how it sees
// (docs/architecture/camera.md, Pose), the directions its rotation means,
// the field of view a drawn view takes from it, and the mix of two poses a
// blend makes (invariant 7).
//
// Contract.
//   1. Location is in metres in the world's axes (right-handed, +y up).
//      Rotation is a yaw and a pitch in degrees. At yaw 0 and pitch 0 the
//      camera looks along -z, the direction an object faces at the
//      identity rotation (bw/world/object.hpp). Positive yaw turns the
//      view to the right (clockwise seen from above); positive pitch
//      turns it up. Near and far are the clip distances in metres.
//   2. The field of view is horizontal, in degrees, across a sensor of
//      sensor_width by sensor_height. vertical_fov_degrees(pose) is the
//      field a drawn view takes, top edge to bottom edge:
//      2 atan(tan(field_of_view / 2) / (sensor_width / sensor_height)).
//      The vertical field is the one held when the target's shape differs
//      from the sensor's. horizontal_fov_degrees(vertical, aspect) is the
//      inverse, 2 atan(tan(vertical / 2) * aspect), for a table that holds
//      a vertical field.
//   3. forward(yaw, pitch) = (sin(yaw) cos(pitch), sin(pitch),
//      -cos(yaw) cos(pitch)); right(yaw) = (cos(yaw), 0, sin(yaw));
//      up(yaw, pitch) = right x forward. The three are unit vectors, and a
//      view drawn from a pose looks along forward with +y as its up hint.
//   4. blend(from, to, factor) for factor in [0, 1]: at 0 it is the first
//      pose and at 1 the second, every field exactly. Between them the
//      location, the field of view, the sensor, near and far are from +
//      factor * (to - from); yaw and pitch are from + factor * d, d the
//      difference to - from brought into (-180, 180], so a turn is
//      blended the short way.
//   5. facing_yaw_degrees(rotation) is the yaw, in [0, 360), of the
//      direction `rotation` turns -z to, read about the up axis in the
//      sense of contract 1: 0 for the identity, 90 for an object turned a
//      quarter to its right. A direction straight up or down has yaw 0.
//   6. A new Pose is at the origin with the sensor kSensorWidth by
//      kSensorHeight and every other number 0; a field of view of 0 means
//      no node has set a lens.
namespace bw::camera {

inline constexpr float kSensorWidth = 24.89F;
inline constexpr float kSensorHeight = 18.67F;

struct Pose {
  math::Vector3f location{math::zero_init};
  float yaw_degrees{};
  float pitch_degrees{};
  // Horizontal, degrees.
  float field_of_view_degrees{};
  float sensor_width{kSensorWidth};
  float sensor_height{kSensorHeight};
  float near_plane{};
  float far_plane{};
};

// Precondition: 0 < pose.field_of_view_degrees < 180 and both sensor sides
// are above 0.
[[nodiscard]] float vertical_fov_degrees(const Pose& pose) noexcept;
// Precondition: 0 < vertical_degrees < 180 and aspect > 0.
[[nodiscard]] float horizontal_fov_degrees(float vertical_degrees, float aspect) noexcept;

[[nodiscard]] math::Vector3f forward(float yaw_degrees, float pitch_degrees) noexcept;
[[nodiscard]] math::Vector3f right(float yaw_degrees) noexcept;
[[nodiscard]] math::Vector3f up(float yaw_degrees, float pitch_degrees) noexcept;

// Precondition: `rotation` is a unit quaternion.
[[nodiscard]] float facing_yaw_degrees(const math::Quaternion4f& rotation) noexcept;

// Precondition: 0 <= factor <= 1.
[[nodiscard]] Pose blend(const Pose& from_pose, const Pose& to_pose, float factor) noexcept;

} // namespace bw::camera
#endif
