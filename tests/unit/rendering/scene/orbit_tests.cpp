// Unit test: the orbit camera (bw/rendering/scene/orbit.hpp), one table
// scenario per contract item or pair. Pure functions over values; no device.
#include "bw/rendering/scene/orbit.hpp"

#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/scene/view.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace {

using bw::math::Vector3f;
using bw::scene::OrbitCamera;
using bw::testing::require;

bool near_relative(float actual, float expected, float tolerance) {
  return std::fabs(actual - expected) <= tolerance * std::fmax(1.0F, std::fabs(expected));
}

bool same_vector(const Vector3f& a, const Vector3f& b) {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool near_vector(const Vector3f& a, const Vector3f& b, float tolerance) {
  return near_relative(a.x, b.x, tolerance) && near_relative(a.y, b.y, tolerance) &&
         near_relative(a.z, b.z, tolerance);
}

// (orbit.hpp, orbit_from_sphere and view_of, contracts 1 and 6): the view of a
// camera at rest is frame_sphere's for the same sphere and aspect: target, up
// and field of view exactly, eye, near and far within float rounding; the
// camera at rest has no offset; and it is empty exactly when frame_sphere is.
void reset_matches_frame_sphere() {
  struct Row {
    Vector3f centre;
    float radius;
    float aspect;
  };
  const std::array rows{Row{Vector3f{1.0F, 2.0F, -3.0F}, 2.5F, 16.0F / 9.0F},
                        Row{Vector3f{0.0F, 0.9F, 0.0F}, 0.75F, 0.5F},
                        Row{Vector3f{1.0F, 2.0F, 3.0F}, 0.0F, 1.0F}};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    const std::optional<bw::scene::PerspectiveView> framed =
        bw::scene::frame_sphere(row.centre, row.radius, row.aspect);
    const std::optional<OrbitCamera> camera =
        bw::scene::orbit_from_sphere(row.centre, row.radius, row.aspect);
    require(camera.has_value() == framed.has_value(), i,
            "the camera is empty exactly when frame_sphere is");
    if (!framed.has_value() || !camera.has_value()) {
      continue;
    }
    require(camera->yaw == 0.0F && camera->pitch == 0.0F, i, "a camera at rest has no turn");
    require(same_vector(camera->offset, Vector3f{bw::math::zero_init}), i,
            "a camera at rest has no offset");
    const bw::scene::PerspectiveView view = bw::scene::view_of(*camera);
    require(same_vector(view.target, framed->target), i, "the target is frame_sphere's");
    require(same_vector(view.up, framed->up), i, "up is frame_sphere's");
    require(view.vertical_fov == framed->vertical_fov, i, "the field of view is frame_sphere's");
    require(near_vector(view.eye, framed->eye, 1.0e-5F), i, "the eye is frame_sphere's");
    require(near_relative(view.near_plane, framed->near_plane, 1.0e-5F), i,
            "the near plane is frame_sphere's");
    require(near_relative(view.far_plane, framed->far_plane, 1.0e-5F), i,
            "the far plane is frame_sphere's");
  }
}

// (orbit.hpp, turn and dolly, contract 2 and 3): a drag changes yaw and pitch
// by radians per pixel, yaw wrapped into (-pi, pi] and pitch clamped to the
// limit at both poles; a scroll scales the distance per line within the range
// of radii at both ends; the centre, radius and offset are untouched.
void turn_and_dolly_clamp() {
  constexpr float kRate = bw::scene::kOrbitRadiansPerPixel;
  constexpr float kLimit = bw::scene::kOrbitPitchLimit;
  constexpr float kTwoPi = 6.28318530717959F;
  const Vector3f centre{1.0F, 2.0F, 3.0F};
  const Vector3f offset{0.5F, -1.0F, 2.0F};
  const auto camera = [&centre, &offset](float yaw, float pitch) {
    return OrbitCamera{centre, 2.0F, offset, yaw, pitch, 10.0F};
  };
  struct Row {
    OrbitCamera start;
    float dx{}, dy{}, lines{};
    float yaw{}, pitch{}, distance{};
  };
  const std::array rows{
      // A drag right lowers yaw; a drag down raises pitch.
      Row{camera(0.0F, 0.0F), 90.0F, 0.0F, 0.0F, -90.0F * kRate, 0.0F, 10.0F},
      Row{camera(0.0F, 0.0F), 0.0F, 90.0F, 0.0F, 0.0F, 90.0F * kRate, 10.0F},
      // The pitch clamp at both poles.
      Row{camera(0.0F, 0.0F), 0.0F, 1000.0F, 0.0F, 0.0F, kLimit, 10.0F},
      Row{camera(0.0F, 0.0F), 0.0F, -1000.0F, 0.0F, 0.0F, -kLimit, 10.0F},
      // The yaw wrap past pi and below -pi.
      Row{camera(3.0F, 0.0F), -36.0F, 0.0F, 0.0F, 3.0F + 36.0F * kRate - kTwoPi, 0.0F, 10.0F},
      Row{camera(-3.0F, 0.0F), 36.0F, 0.0F, 0.0F, -3.0F - 36.0F * kRate + kTwoPi, 0.0F, 10.0F},
      // A factor per line, both ways, and the distance clamp at both ends.
      Row{camera(0.0F, 0.0F), 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 10.0F * 1.1F},
      Row{camera(0.0F, 0.0F), 0.0F, 0.0F, -2.0F, 0.0F, 0.0F, 10.0F / (1.1F * 1.1F)},
      Row{camera(0.0F, 0.0F), 0.0F, 0.0F, 100.0F, 0.0F, 0.0F, bw::scene::kOrbitMaxRadii * 2.0F},
      Row{camera(0.0F, 0.0F), 0.0F, 0.0F, -100.0F, 0.0F, 0.0F, bw::scene::kOrbitMinRadii * 2.0F},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    const OrbitCamera turned = bw::scene::turn(row.start, row.dx, row.dy, kRate);
    const OrbitCamera moved = bw::scene::dolly(turned, row.lines);
    require(near_relative(moved.yaw, row.yaw, 1.0e-5F), i,
            "yaw " + std::to_string(moved.yaw) + ", expected " + std::to_string(row.yaw));
    require(near_relative(moved.pitch, row.pitch, 1.0e-5F), i,
            "pitch " + std::to_string(moved.pitch) + ", expected " + std::to_string(row.pitch));
    require(near_relative(moved.distance, row.distance, 1.0e-5F), i,
            "distance " + std::to_string(moved.distance) + ", expected " +
                std::to_string(row.distance));
    require(moved.yaw > -3.14159265F && moved.yaw <= 3.14159266F, i, "yaw is in (-pi, pi]");
    require(turned.distance == row.start.distance, i, "turn leaves the distance");
    require(moved.yaw == turned.yaw && moved.pitch == turned.pitch, i, "dolly leaves the angles");
    require(same_vector(moved.centre, centre) && moved.radius == row.start.radius &&
                same_vector(moved.offset, offset),
            i, "the centre, radius and offset are untouched");
  }
}

// (orbit.hpp, pan, contract 4): a drag of (dx, dy) pixels adds -dx * wpp along
// the right vector (cos(yaw), 0, -sin(yaw)) and dy * wpp along the up vector,
// right cross the eye-to-target direction; at rest those are +x and +y. The
// other fields are untouched.
void pan_slides_offset() {
  const Vector3f centre{1.0F, 2.0F, 3.0F};
  struct Row {
    Vector3f start{bw::math::zero_init};
    float yaw{}, pitch{}, dx{}, dy{}, wpp{};
    Vector3f expected{bw::math::zero_init};
  };
  // A turned camera's right and up vectors, from contract 4's formula.
  constexpr float kYaw = 1.57079632679F;
  constexpr float kPitch = 0.5F;
  const Vector3f right{std::cos(kYaw), 0.0F, -std::sin(kYaw)};
  const Vector3f up = right.cross(Vector3f{-std::cos(kPitch) * std::sin(kYaw), -std::sin(kPitch),
                                           -std::cos(kPitch) * std::cos(kYaw)});
  const Vector3f start{0.5F, -1.0F, 2.0F};
  const std::array rows{
      // At rest a drag right moves the offset -x and a drag up moves it -y.
      Row{Vector3f{bw::math::zero_init}, 0.0F, 0.0F, 30.0F, -20.0F, 0.25F,
          Vector3f{-7.5F, -5.0F, 0.0F}},
      Row{start, kYaw, kPitch, 12.0F, 7.0F, 0.1F,
          start + right * (-12.0F * 0.1F) + up * (7.0F * 0.1F)},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    const OrbitCamera before{centre, 2.0F, row.start, row.yaw, row.pitch, 10.0F};
    const OrbitCamera panned = bw::scene::pan(before, row.dx, row.dy, row.wpp);
    require(near_vector(panned.offset, row.expected, 1.0e-5F), i,
            "offset " + std::to_string(panned.offset.x) + "," + std::to_string(panned.offset.y) +
                "," + std::to_string(panned.offset.z));
    require(same_vector(panned.centre, centre) && panned.radius == before.radius &&
                panned.yaw == before.yaw && panned.pitch == before.pitch &&
                panned.distance == before.distance,
            i, "pan leaves the other fields");
  }
}

// (orbit.hpp, view_of, contract 6): with an offset the target is the centre
// plus the offset, the eye is distance along the angles' direction from it,
// and the planes are the contract's values for s, the eye's distance from
// the centre. Each row is a camera at rest panned along +x, then turned a
// quarter turn so it looks along the x axis. Near side: panned a little short
// of distance - radius and turned to yaw -pi/2, it looks back through the
// sphere at a target beyond it, the eye 0.02 * distance outside the sphere,
// where frame_sphere's near fraction would cut the sphere: the near plane is
// the half gap and the far plane the far fraction. Far side: panned by
// 3 * distance and turned to yaw pi/2, it looks away from the sphere behind
// the eye's target: the near plane is the near fraction and the far plane the
// margin past the sphere. So each formula's two branches are told apart.
void panned_planes_hold_sphere() {
  const Vector3f centre{0.0F, 1.0F, 0.0F};
  const std::optional<OrbitCamera> rest = bw::scene::orbit_from_sphere(centre, 1.0F, 1.0F);
  require(rest.has_value(), "a unit sphere frames");
  if (!rest.has_value()) {
    return;
  }
  const float d = rest->distance;
  const float r = rest->radius;
  struct Row {
    float slide{}, turn{};
    std::string_view note;
  };
  constexpr float kQuarter = 1.57079632679F;
  const std::array rows{
      Row{d - r - 0.02F * d, kQuarter, "near side: the near fit and the far fraction"},
      Row{3.0F * d, -kQuarter, "far side: the near fraction and the far fit"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    const OrbitCamera camera =
        bw::scene::turn(bw::scene::pan(*rest, -row.slide, 0.0F, 1.0F), row.turn, 0.0F, 1.0F);
    const bw::scene::PerspectiveView view = bw::scene::view_of(camera);
    const Vector3f target = centre + Vector3f{row.slide, 0.0F, 0.0F};
    const Vector3f eye =
        target + Vector3f{std::cos(camera.pitch) * std::sin(camera.yaw), std::sin(camera.pitch),
                          std::cos(camera.pitch) * std::cos(camera.yaw)} *
                     d;
    require(near_vector(view.target, target, 1.0e-6F), i,
            "the target is the centre plus the offset");
    require(near_vector(view.eye, eye, 1.0e-5F), i, "the eye is the contract's");
    const float s = (eye - centre).length();
    const float near_plane = std::fmin(bw::scene::kFrameNear * d,
                                       std::fmax((s - r) / 2.0F, bw::scene::kOrbitNearFloor * d));
    const float far_plane = std::fmax(bw::scene::kFrameFar * d, bw::scene::kFrameMargin * (s + r));
    require(near_relative(view.near_plane, near_plane, 1.0e-5F), i,
            std::string(row.note) + ": near " + std::to_string(view.near_plane) + ", expected " +
                std::to_string(near_plane));
    require(near_relative(view.far_plane, far_plane, 1.0e-5F), i,
            std::string(row.note) + ": far " + std::to_string(view.far_plane) + ", expected " +
                std::to_string(far_plane));
    if (i == 0U) {
      require(bw::scene::kFrameNear * d > s - r, i,
              "frame_sphere's near fraction would cut the sphere");
    } else {
      require(bw::scene::kFrameMargin * (s + r) > bw::scene::kFrameFar * d, i,
              "the far fraction would cut the sphere");
    }
  }
}

using Scenario = bw::testing::Scenario<void>;

constexpr std::array kScenarios{
    Scenario{"reset_matches_frame_sphere", &reset_matches_frame_sphere},
    Scenario{"turn_and_dolly_clamp", &turn_and_dolly_clamp},
    Scenario{"pan_slides_offset", &pan_slides_offset},
    Scenario{"panned_planes_hold_sphere", &panned_planes_hold_sphere},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
