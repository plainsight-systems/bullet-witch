// In-process view contracts within the existing resource test executable.
// Independent frustum boundary points verify handedness, vertical FoV, aspect,
// reversed depth and translated/rotated eyes; material vectors retain world
// axes. Invalid numerical inputs must reject rather than normalize or publish
// unusable transforms. Fixed fixtures allocate no engine state. Both MCP
// phases: I.1 (#ri-explicit) tests public coordinates; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps fixed arrays.
#include "bw/render/scene/view.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void near(float actual, float expected, const char* message) {
  constexpr float kTolerance = 0.0001F;
  require(std::abs(actual - expected) < kTolerance, message);
}
float evaluate(const std::array<float, 4>& row,
               const std::array<float, 3>& point) {
  return row[0] * point[0] + row[1] * point[1] + row[2] * point[2] + row[3];
}
void require_rejected(const bw::render::ViewPose& pose,
                      const bw::render::PerspectiveView& perspective) {
  bool rejected = false;
  try {
    bw::render::make_view_transforms(pose, perspective);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Invalid view input must reject");
}
}  // namespace
void view_tests() {
  using bw::render::make_view_transforms;
  // A translated camera looking along world +X exposes accidental transposes
  // and the mistaken use of view rotation for material-light vectors.
  const bw::render::ViewPose pose{
      {10, 20, 30}, {0, 0, -1}, {0, 1, 0}, {1, 0, 0}};
  const bw::render::PerspectiveView perspective{std::numbers::pi_v<float> / 2,
                                                2, 1, 101};
  const auto view = make_view_transforms(pose, perspective);
  const std::array near_point{11.0F, 20.0F, 30.0F};
  const std::array far_point{111.0F, 20.0F, 30.0F};
  near(evaluate(view.clip_rows[2], near_point) /
           evaluate(view.clip_rows[3], near_point),
       1, "Near plane maps to reversed depth one");
  near(evaluate(view.clip_rows[2], far_point) /
           evaluate(view.clip_rows[3], far_point),
       0, "Far plane maps to reversed depth zero");
  const std::array corner{11.0F, 21.0F, 28.0F};
  near(evaluate(view.clip_rows[0], corner), 1,
       "Horizontal boundary uses vertical FoV times aspect");
  near(evaluate(view.clip_rows[1], corner), 1,
       "Vertical boundary retains upward screen axis");
  near(evaluate(view.clip_rows[3], corner), 1, "Clip W equals positive view Z");
  near(evaluate(view.view_rows[0], corner), 2,
       "View rotates world to camera right");
  near(evaluate(view.camera_relative_world_rows[0], corner), 1,
       "Material X retains global axis");
  near(evaluate(view.camera_relative_world_rows[2], corner), -2,
       "Material Z is translated without camera rotation");
  near(evaluate(view.clip_rows[3], std::array{9.0F, 20.0F, 30.0F}), -1,
       "Behind-camera points have negative clip W");

  auto invalid_pose = pose;
  invalid_pose.up = pose.right;
  require_rejected(invalid_pose, perspective);
  invalid_pose = pose;
  invalid_pose.forward = {-1, 0, 0};
  require_rejected(invalid_pose, perspective);
  invalid_pose = pose;
  invalid_pose.eye[0] = std::numeric_limits<float>::quiet_NaN();
  require_rejected(invalid_pose, perspective);
  auto invalid_projection = perspective;
  invalid_projection.near_plane = perspective.far_plane;
  require_rejected(pose, invalid_projection);
  invalid_projection = perspective;
  invalid_projection.aspect = 0;
  require_rejected(pose, invalid_projection);
  invalid_projection = perspective;
  invalid_projection.vertical_fov_radians = std::numbers::pi_v<float>;
  require_rejected(pose, invalid_projection);
  invalid_projection = perspective;
  invalid_projection.near_plane = std::numeric_limits<float>::min();
  invalid_projection.far_plane = std::numeric_limits<float>::max();
  require_rejected(pose, invalid_projection);
}
