#include "bw/rendering/scene/view.hpp"

#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/platform/math/math.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <cstddef>
#include <optional>

// Responsibility: the out-of-line part of bw/rendering/scene/view.hpp.
namespace bw::scene {

math::Matrix4f world_to_clip(const PerspectiveView& view, float aspect) {
  return math::Matrix4f::look_at(view.eye, view.target, view.up) *
         math::Matrix4f::perspective(view.vertical_fov, aspect, view.near_plane, view.far_plane);
}

shaders::Float4x4 shader_matrix(const math::Matrix4f& matrix) noexcept {
  shaders::Float4x4 out{};
  for (std::size_t r = 0; r < 4; ++r) {
    out.rows[r] = shaders::Float4{matrix.m[r][0], matrix.m[r][1], matrix.m[r][2], matrix.m[r][3]};
  }
  return out;
}

// The radius and the aspect in the order the header documents.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::optional<PerspectiveView> frame_sphere(const math::Vector3f& center, float radius,
                                            float aspect) noexcept {
  if (center.contains_non_finite() || !platform::math::is_finite(radius) || !(radius > 0.0F)) {
    return std::nullopt;
  }
  const float half_vertical = kFrameVerticalFov / 2.0F;
  const float half_horizontal =
      platform::math::arc_tangent(platform::math::tangent(half_vertical) * aspect);
  const float half_narrow = half_horizontal < half_vertical ? half_horizontal : half_vertical;
  const float distance = kFrameMargin * radius / platform::math::sine(half_narrow);
  if (!platform::math::is_finite(distance)) {
    return std::nullopt;
  }
  PerspectiveView view{};
  view.target = center;
  view.eye = center + math::Vector3f{0.0F, 0.0F, distance};
  // look_at needs the eye apart from the target by more than its tolerance;
  // a sphere too small, or too far from the origin for a float to hold the
  // step, does not give that.
  if (!((view.eye - view.target).length_squared() > 1.0e-8F)) {
    return std::nullopt;
  }
  view.vertical_fov = kFrameVerticalFov;
  view.near_plane = kFrameNear * distance;
  view.far_plane = kFrameFar * distance;
  return view;
}

} // namespace bw::scene
