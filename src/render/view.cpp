// Build portable camera transforms from explicit validated pose/frustum inputs.
// The original renderer separates rotated clip coordinates from camera-relative
// global material vectors; preserve both outputs rather than rotating lighting
// with the camera. Validation precedes local rigid inversion and reversed-depth
// composition; failure publishes nothing. No shared state or successful-path
// allocation occurs. Scope, original-source mapping and costs are in view.hpp
// and player-camera-initialization.md. Both MCP phases: cpp-guidelines I.1
// (#ri-explicit) makes coordinate contracts visible; cpp-performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps fixed local
// arrays.
#include "bw/render/view.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace bw::render {
namespace {
using Vector = std::array<float, 3>;
constexpr float kBasisTolerance = 0.0001F;
float dot(const Vector& left, const Vector& right) {
  return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
}
bool near(float value, float expected) {
  return std::abs(value - expected) <= kBasisTolerance;
}
template <std::size_t Size>
bool finite(const std::array<float, Size>& values) {
  return std::ranges::all_of(values,
                             [](float value) { return std::isfinite(value); });
}
void validate(const ViewPose& pose, const PerspectiveView& perspective) {
  const auto& right = pose.right;
  const auto& up = pose.up;
  const auto& forward = pose.forward;
  const Vector cross{right[1] * up[2] - right[2] * up[1],
                     right[2] * up[0] - right[0] * up[2],
                     right[0] * up[1] - right[1] * up[0]};
  if (!finite(pose.eye) || !finite(right) || !finite(up) || !finite(forward) ||
      !near(dot(right, right), 1) || !near(dot(up, up), 1) ||
      !near(dot(forward, forward), 1) || !near(dot(right, up), 0) ||
      !near(dot(right, forward), 0) || !near(dot(up, forward), 0) ||
      !near(dot(cross, forward), 1)) {
    throw std::invalid_argument(
        "View requires a finite orthonormal camera pose");
  }
  if (!finite(std::array{perspective.vertical_fov_radians, perspective.aspect,
                         perspective.near_plane, perspective.far_plane}) ||
      perspective.vertical_fov_radians <= 0 ||
      perspective.vertical_fov_radians >= std::numbers::pi_v<float> ||
      perspective.aspect <= 0 || perspective.near_plane <= 0 ||
      perspective.far_plane <= perspective.near_plane) {
    throw std::invalid_argument(
        "View requires a finite valid perspective frustum");
  }
}
}  // namespace
ViewTransforms make_view_transforms(const ViewPose& pose,
                                    const PerspectiveView& perspective) {
  validate(pose, perspective);
  ViewTransforms result;
  // Rigid inversion projects the eye-relative point onto each camera axis.
  const std::array basis{pose.right, pose.up, pose.forward};
  for (std::size_t axis = 0; axis < basis.size(); ++axis) {
    const auto& direction = basis.at(axis);
    result.view_rows.at(axis) = {direction[0], direction[1], direction[2],
                                 -dot(direction, pose.eye)};
    result.camera_relative_world_rows.at(axis).at(axis) = 1;
    result.camera_relative_world_rows.at(axis)[3] = -pose.eye.at(axis);
  }
  // Positive forward distance becomes clip W; finite reversed depth maps the
  // near plane to one and far plane to zero without a shader-side remapping.
  const float vertical = 1 / std::tan(perspective.vertical_fov_radians * 0.5F);
  const float horizontal = vertical / perspective.aspect;
  const float depth =
      perspective.near_plane / (perspective.near_plane - perspective.far_plane);
  if (!(vertical > 0) || !(horizontal > 0) || !(depth < 0)) {
    throw std::invalid_argument(
        "Perspective coefficients collapse at float precision");
  }
  for (std::size_t component = 0; component < result.clip_rows[0].size();
       ++component) {
    result.clip_rows[0].at(component) =
        result.view_rows[0].at(component) * horizontal;
    result.clip_rows[1].at(component) =
        result.view_rows[1].at(component) * vertical;
    result.clip_rows[2].at(component) =
        result.view_rows[2].at(component) * depth;
    result.clip_rows[3].at(component) = result.view_rows[2].at(component);
  }
  result.clip_rows[2][3] -= depth * perspective.far_plane;
  // Finite inputs alone do not guarantee representable transformed origins.
  if (!std::ranges::all_of(result.view_rows, finite<4>) ||
      !std::ranges::all_of(result.clip_rows, finite<4>)) {
    throw std::invalid_argument("Camera transform exceeds finite float range");
  }
  return result;
}
}  // namespace bw::render
