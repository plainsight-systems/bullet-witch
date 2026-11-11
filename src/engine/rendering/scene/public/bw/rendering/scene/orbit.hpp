#ifndef BW_RENDERING_SCENE_ORBIT_HPP
#define BW_RENDERING_SCENE_ORBIT_HPP

#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/scene/view.hpp"

#include <optional>

// Responsibility: the orbit camera a viewer moves (docs/architecture/
// source-layout.md, rendering/scene: views): a value placed on a sphere
// about a target, turned by a drag, moved along its ray by a scroll, slid
// in its own plane by a second drag, and turned into the PerspectiveView of
// view.hpp. The target starts at a model's bounding-sphere centre and a pan
// moves it by an offset; the sphere stays known, so the clip planes hold it
// wherever the target has gone. Pure functions over values: no time, no
// input types, no OS.
//
// Contract.
//   1. orbit_from_sphere(centre, radius, aspect) is the camera at rest:
//      yaw 0, pitch 0, zero offset, and distance the eye distance
//      frame_sphere(centre, radius, aspect) gives, so view_of of it is that
//      view: the same target, up and vertical field of view, and an eye,
//      near plane and far plane equal to frame_sphere's within float
//      rounding of the distance. It is empty exactly when frame_sphere is
//      empty.
//   2. turn(camera, dx, dy, radians_per_pixel) is the camera turned by a
//      drag of dx pixels right and dy pixels down: yaw decreases by
//      dx * radians_per_pixel and is wrapped into (-pi, pi]; pitch
//      increases by dy * radians_per_pixel and is clamped to
//      [-kOrbitPitchLimit, kOrbitPitchLimit]; the other fields are
//      unchanged. Dragging right moves the eye to its left around the
//      target, so the model turns with the pointer; dragging down raises
//      the eye, so the camera looks further down.
//   3. dolly(camera, lines) is the camera moved along its ray to the
//      target: distance is multiplied by kOrbitDollyPerLine to the power
//      lines, then clamped to [kOrbitMinRadii, kOrbitMaxRadii] * radius.
//      Positive lines (a wheel rolled toward the user with the default
//      scroll direction) move the eye away; the other fields are unchanged.
//   4. pan(camera, dx, dy, world_per_pixel) is the camera slid in its own
//      plane by a drag of dx pixels right and dy pixels down: offset moves
//      by -dx * world_per_pixel along the camera's right vector,
//      (cos(yaw), 0, -sin(yaw)), and by dy * world_per_pixel along its up
//      vector, the cross product of that right vector and the direction
//      from the eye of contract 6 to the target (+y at pitch 0); the other
//      fields are unchanged, and the eye moves with the target. The model
//      follows the pointer: dragging right moves it right on screen,
//      dragging down moves it down. The offset is not bounded; a new camera
//      from orbit_from_sphere zeroes it.
//   5. world_per_pixel(camera, viewport_height) is the width in world units
//      of one pixel at the target's depth: 2 * distance *
//      tan(kFrameVerticalFov / 2) / viewport_height, so a pan by it keeps a
//      point at that depth under the pointer. Precondition: viewport_height
//      is positive.
//   6. view_of(camera): target the centre plus the offset, up +y,
//      vertical_fov kFrameVerticalFov, eye the target plus distance *
//      (cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw)). The
//      planes hold the sphere: with s the eye's distance from the centre,
//      near_plane is the smaller of kFrameNear * distance and the larger of
//      (s - radius) / 2 and kOrbitNearFloor * distance, and far_plane is
//      the larger of kFrameFar * distance and kFrameMargin * (s + radius).
//      With zero offset s is distance: far_plane is then kFrameFar *
//      distance throughout dolly's range, and near_plane is kFrameNear *
//      distance at every distance of at least radius / (1 - 2 * kFrameNear),
//      about 1.11 radii, which holds for every rest camera of contract 1;
//      between kOrbitMinRadii and that the near plane is the half gap to
//      the sphere, nearer than the fraction. The pitch clamp keeps the eye off
//      the vertical through the target, so look_at's precondition holds.
//      Precondition: the camera came from orbit_from_sphere through turn,
//      dolly and pan.
//
// Why. The rest camera uses frame_sphere, the framing game_host already
// draws with, so the reset image is game_host's first frame. The viewer
// shows one model, so a dolly by a factor per line within a range of radii
// is the whole need. The planes are frame_sphere's fractions of the
// distance while those hold the sphere, and otherwise the near plane halves
// the gap to the sphere and the far plane clears it with frame_sphere's
// margin, so a panned then turned model, or one dollied to the bottom of
// the range, is clipped by neither. A pan is scaled by the pixel's width at
// the target's depth (contract 5), so the point dragged stays under the
// pointer, and the sphere is kept apart from the offset for contract 6's
// planes. The distance the user chose is kept across a resize; R refits it
// to the new aspect.
//
// Core guidelines read: I.1 (every input is an argument), Con.5 (the
// constants are constexpr), F.20 (a camera is returned by value), E.27 (an
// unframeable sphere is the empty optional frame_sphere gives, not a
// default camera).
namespace bw::scene {

struct OrbitCamera {
  // The model's bounding sphere; the target is centre + offset.
  math::Vector3f centre{math::zero_init};
  float radius{};
  math::Vector3f offset{math::zero_init};
  // Radians: yaw about +y through the target; pitch the eye's elevation
  // above the plane through the target.
  float yaw{};
  float pitch{};
  float distance{};
};

inline constexpr float kOrbitPitchLimit = 3.14159265358979F / 2.0F - 0.01F;
// Radians per pixel of drag; the viewer's turn rate.
inline constexpr float kOrbitRadiansPerPixel = 1.0F / 180.0F;
inline constexpr float kOrbitDollyPerLine = 1.1F;
// A held key turns or pans at this many drag pixels per second and dollies
// at this many lines per second; the viewer multiplies by the frame's
// seconds.
// The step is applied directly, with no easing: the viewer is a check, not
// a camera to fly. The arrows turn and W, A, S and D pan, at one rate, and
// every key has the drag's sign, so a key turns or slides the model the way
// a drag in that direction does.
inline constexpr float kOrbitKeyPixelsPerSecond = 180.0F;
inline constexpr float kOrbitKeyLinesPerSecond = 6.0F;
inline constexpr float kOrbitMinRadii = 1.1F;
inline constexpr float kOrbitMaxRadii = 20.0F;
// The near plane's floor as a fraction of the distance, for an eye inside
// or against the sphere (contract 6).
inline constexpr float kOrbitNearFloor = 0.001F;

[[nodiscard]] std::optional<OrbitCamera> orbit_from_sphere(const math::Vector3f& centre,
                                                           float radius, float aspect) noexcept;

[[nodiscard]] OrbitCamera turn(OrbitCamera camera, float dx, float dy,
                               float radians_per_pixel) noexcept;

[[nodiscard]] OrbitCamera dolly(OrbitCamera camera, float lines) noexcept;

[[nodiscard]] OrbitCamera pan(OrbitCamera camera, float dx, float dy,
                              float world_per_pixel) noexcept;

[[nodiscard]] float world_per_pixel(const OrbitCamera& camera, float viewport_height) noexcept;

[[nodiscard]] PerspectiveView view_of(const OrbitCamera& camera) noexcept;

} // namespace bw::scene

#endif
