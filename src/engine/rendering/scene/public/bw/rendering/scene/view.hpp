#ifndef BW_RENDERING_SCENE_VIEW_HPP
#define BW_RENDERING_SCENE_VIEW_HPP

#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <optional>

// Responsibility: a view of the scene as data, and the matrix a draw needs
// from it (docs/architecture/source-layout.md, rendering/scene: render-owned
// proxies, views and visibility). A host states where the camera is; the
// matrices come from bw/math/matrix/matrix4.hpp and nothing here or in a
// host writes matrix elements by hand.
//
// Contract.
//   1. world_to_clip(view, aspect) ==
//      Matrix4f::look_at(view.eye, view.target, view.up) *
//      Matrix4f::perspective(view.vertical_fov, aspect, view.near_plane,
//      view.far_plane), with the preconditions of both.
//   2. shader_matrix(m) is m's four rows as the parameter-block type; no
//      element changes.
//   3. frame_sphere(center, radius, aspect) is the view that holds a
//      sphere whatever turns inside it: target the center, eye the center
//      plus (0, 0, d), up +y, vertical_fov kFrameVerticalFov. d is
//      kFrameMargin * radius / sin(h), h the smaller of vertical_fov / 2
//      and atan(tan(vertical_fov / 2) * aspect), so the sphere is inside
//      all four side planes with the margin to spare. near_plane is
//      kFrameNear * d and far_plane kFrameFar * d; since h is at most 20
//      degrees, radius is at most 0.33 d and the sphere lies between them
//      at every aspect. The result is empty when the sphere cannot be
//      framed: a component of the center or the radius is not finite, the
//      radius is not positive, d is not finite, or the eye is not apart
//      from the center by more than look_at's tolerance (a sphere too
//      small, or too far from the origin for a float to hold the step).
//      Nothing else checks a mesh's bounds before they reach a view.
//      Precondition: aspect > 0.
//
// Read guidance: I.1 (a view is every input of its matrix), F.20.
namespace bw::scene {

struct PerspectiveView {
  math::Vector3f eye{math::zero_init};
  math::Vector3f target{math::zero_init};
  math::Vector3f up{0.0F, 1.0F, 0.0F};
  // Radians, top edge to bottom edge.
  float vertical_fov{};
  float near_plane{};
  float far_plane{};
};

[[nodiscard]] math::Matrix4f world_to_clip(const PerspectiveView& view, float aspect);

[[nodiscard]] shaders::Float4x4 shader_matrix(const math::Matrix4f& matrix) noexcept;

// 40 degrees top to bottom; a 5 percent margin; near and far as fractions of
// the eye's distance.
inline constexpr float kFrameVerticalFov = 40.0F * 3.14159265358979F / 180.0F;
inline constexpr float kFrameMargin = 1.05F;
inline constexpr float kFrameNear = 0.05F;
inline constexpr float kFrameFar = 4.0F;

[[nodiscard]] std::optional<PerspectiveView> frame_sphere(const math::Vector3f& center,
                                                          float radius, float aspect) noexcept;

} // namespace bw::scene

#endif
