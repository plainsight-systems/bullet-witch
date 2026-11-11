#include "bw/rendering/scene/orbit.hpp"

#include "bw/math/vector/vector3.hpp"
#include "bw/platform/math/math.hpp"
#include "bw/rendering/scene/view.hpp"

#include <cmath>
#include <optional>

// Responsibility: the out-of-line part of bw/rendering/scene/orbit.hpp.
namespace bw::scene {
namespace {

constexpr float kPi = 3.14159265358979F;
constexpr float kTwoPi = 2.0F * kPi;

// An angle in (-pi, pi]: the multiple of a turn that brings it there is
// subtracted, so a yaw never grows over a long drag.
float wrap_angle(float radians) { return radians - kTwoPi * std::ceil((radians - kPi) / kTwoPi); }

float clamp(float value, float low, float high) {
  return value < low ? low : (value > high ? high : value);
}

} // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::optional<OrbitCamera> orbit_from_sphere(const math::Vector3f& centre, float radius,
                                             float aspect) noexcept {
  const std::optional<PerspectiveView> framed = frame_sphere(centre, radius, aspect);
  if (!framed.has_value()) {
    return std::nullopt;
  }
  OrbitCamera camera{};
  camera.centre = centre;
  camera.radius = radius;
  camera.yaw = 0.0F;
  camera.pitch = 0.0F;
  camera.distance = (framed->eye - centre).length();
  return camera;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
OrbitCamera turn(OrbitCamera camera, float dx, float dy, float radians_per_pixel) noexcept {
  camera.yaw = wrap_angle(camera.yaw - dx * radians_per_pixel);
  camera.pitch = clamp(camera.pitch + dy * radians_per_pixel, -kOrbitPitchLimit, kOrbitPitchLimit);
  return camera;
}

OrbitCamera dolly(OrbitCamera camera, float lines) noexcept {
  const float scaled = camera.distance * std::pow(kOrbitDollyPerLine, lines);
  camera.distance = clamp(scaled, kOrbitMinRadii * camera.radius, kOrbitMaxRadii * camera.radius);
  return camera;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
OrbitCamera pan(OrbitCamera camera, float dx, float dy, float world_per_pixel) noexcept {
  const float cos_yaw = platform::math::cosine(camera.yaw);
  const float sin_yaw = platform::math::sine(camera.yaw);
  const float cos_pitch = platform::math::cosine(camera.pitch);
  const float sin_pitch = platform::math::sine(camera.pitch);
  const math::Vector3f right{cos_yaw, 0.0F, -sin_yaw};
  // The direction from the eye to the target, the negation of view_of's eye direction.
  const math::Vector3f forward{-cos_pitch * sin_yaw, -sin_pitch, -cos_pitch * cos_yaw};
  const math::Vector3f up = right.cross(forward);
  camera.offset = camera.offset + right * (-dx * world_per_pixel) + up * (dy * world_per_pixel);
  return camera;
}

float world_per_pixel(const OrbitCamera& camera, float viewport_height) noexcept {
  return 2.0F * camera.distance * platform::math::tangent(kFrameVerticalFov / 2.0F) /
         viewport_height;
}

PerspectiveView view_of(const OrbitCamera& camera) noexcept {
  const float cos_pitch = platform::math::cosine(camera.pitch);
  const math::Vector3f direction{cos_pitch * platform::math::sine(camera.yaw),
                                 platform::math::sine(camera.pitch),
                                 cos_pitch * platform::math::cosine(camera.yaw)};
  PerspectiveView view{};
  view.target = camera.centre + camera.offset;
  view.eye = view.target + direction * camera.distance;
  view.up = math::Vector3f{0.0F, 1.0F, 0.0F};
  view.vertical_fov = kFrameVerticalFov;
  // The planes hold the sphere wherever the pan has put it: the near plane halves the gap from
  // the eye to the sphere (never below a floor fraction of the distance) and the far plane clears
  // the sphere's far side with frame_sphere's margin.
  const float eye_to_centre = (view.eye - camera.centre).length();
  view.near_plane =
      std::fmin(kFrameNear * camera.distance, std::fmax((eye_to_centre - camera.radius) / 2.0F,
                                                        kOrbitNearFloor * camera.distance));
  view.far_plane =
      std::fmax(kFrameFar * camera.distance, kFrameMargin * (eye_to_centre + camera.radius));
  return view;
}

} // namespace bw::scene
