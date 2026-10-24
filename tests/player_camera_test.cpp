// Exercise independent look/follow state, authored rates, offset recovery and
// atomic rejection through the public camera owner. Synthetic collision keeps
// numerical expectations independent from game assets. Both MCP phases: I.1
// (ri-explicit) tests observable contracts; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) checks fixed retained
// state through the injected memory domain. Native traversal is a separate
// gate.
#include "game/camera/player_camera.hpp"

#include <cmath>
#include <limits>
#include <numbers>

#include "camera_fixture.hpp"
namespace {
using camera_fixture::require;
bool close(float left, float right) { return std::abs(left - right) < .00001F; }
void look_and_follow(const bw::formats::PlayerCameraSettings& settings,
                     const bw::scene::StaticCollision& collision) {
  using bw::game::PlayerCamera;
  constexpr float radians = std::numbers::pi_v<float> / 180.0F;
  PlayerCamera camera(settings, {{0, 0, 0}, 0}, collision);
  const auto initial = camera.view();
  camera.advance({{10, 2, 20}, 180}, {}, .1);
  require(close(camera.yaw_radians(), 0) &&
              close(camera.view().pose.eye[0], initial.pose.eye[0] + 10) &&
              close(camera.view().pose.eye[1], initial.pose.eye[1] + 2) &&
              close(camera.view().pose.eye[2], initial.pose.eye[2] + 20),
          "Translation follows without adopting actor-facing yaw");
  camera.advance({{10, 2, 20}, 180}, {1, .5F}, .3);
  require(close(camera.yaw_radians(), 30 * radians) &&
              close(camera.pitch_radians(), 15 * radians),
          "Authored ordinary look rate consumes seconds and normalized axes");
  PlayerCamera split(settings, {{10, 2, 20}, 0}, collision);
  for (int frame = 0; frame < 30; ++frame) {
    split.advance({{10, 2, 20}, 0}, {1, .5F}, .01);
  }
  require(close(camera.yaw_radians(), split.yaw_radians()) &&
              close(camera.pitch_radians(), split.pitch_radians()),
          "Look integration is independent of render update count");
  camera.advance({{10, 2, 20}, 0}, {0, 1}, 1);
  require(close(camera.pitch_radians(), 70 * radians),
          "Look stops at the authored upper pitch limit");
  const auto view = camera.view();
  bool rejected = false;
  try {
    camera.advance({{10, 2, 20}, 0},
                   {std::numeric_limits<float>::quiet_NaN(), 0}, .1);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && camera.view().pose.eye == view.pose.eye &&
              camera.view().pose.forward == view.pose.forward,
          "Invalid look preserves the published view");
  camera.teleport({{1, 2, 3}, 90});
  require(close(camera.yaw_radians(), 90 * radians) &&
              camera.pitch_radians() == 0 &&
              close(camera.view().pose.eye[0], -2) &&
              close(camera.view().pose.eye[1], 2.7F) &&
              close(camera.view().pose.eye[2], 2.5F),
          "Teleport resets orientation and snaps authored offsets");
}
void context_transitions(const bw::formats::PlayerCameraSettings& settings,
                         const bw::scene::StaticCollision& collision) {
  using bw::game::PlayerCameraContext;
  constexpr float radians = std::numbers::pi_v<float> / 180;
  bw::game::PlayerCamera camera(settings, {{0, 0, 0}, 0}, collision);
  const auto initial = camera.view();
  camera.set_context(PlayerCameraContext::targeting, false);
  require(camera.view().pose.eye == initial.pose.eye &&
              camera.view().perspective.vertical_fov_radians ==
                  initial.perspective.vertical_fov_radians,
          "Context request captures visible state without consuming time");
  camera.advance({{0, 0, 0}, 0}, {}, .125);
  const auto expected = 2 * std::atan(.2F * std::tan(22 * radians) +
                                      .8F * std::tan(10 * radians));
  require(
      close(camera.view().perspective.vertical_fov_radians, expected),
      "Source rational weight interpolates frustum extents, not FOV angles");
  camera.set_context(PlayerCameraContext::targeting, false);
  camera.advance({{0, 0, 0}, 0}, {}, .125);
  require(close(camera.view().perspective.vertical_fov_radians, 20 * radians),
          "Repeated context selection does not restart the transition");
  camera.advance({{0, 0, 0}, 0}, {1, .5F}, .1);
  require(close(camera.yaw_radians(), 4.5F * radians) &&
              close(camera.pitch_radians(), 2.25F * radians),
          "Target context consumes distinct authored look rates");
  camera.set_context(PlayerCameraContext::ordinary, true);
  const auto before = camera.view();
  camera.advance({{0, 0, 0}, 0}, {}, 0);
  require(close(camera.view().perspective.vertical_fov_radians,
                before.perspective.vertical_fov_radians) &&
              camera.view().pose.eye == before.pose.eye,
          "Zero-time context publication preserves the outgoing snapshot");
  camera.advance({{0, 0, 0}, 0}, {}, .25);
  require(close(camera.view().perspective.vertical_fov_radians, 44 * radians),
          "Target exit restores ordinary FOV independently of actor animation");
  camera.teleport({{0, 0, 0}, 0});
  require(close(camera.view().pose.eye[1], .25F),
          "Crouched ordinary preset retains authored camera height");
  bool rejected = false;
  try {
    camera.set_context(static_cast<PlayerCameraContext>(9), false);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && close(camera.view().pose.eye[1], .25F),
          "Invalid context preserves the displayed view");
}
}  // namespace
void player_camera_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  {
    const auto bytes = camera_fixture::customization();
    const auto tree =
        bw::formats::CustomizationDefinition::parse(bytes, domain);
    const auto settings =
        bw::formats::PlayerCameraSettings::parse_initial(tree);
    bw::scene::StaticCollision collision(domain);
    look_and_follow(settings, collision);
    context_transitions(settings, collision);
    const auto wall = camera_fixture::collision(
        "wall", 0x64, {{{-10, -10, -1}, {10, -10, -1}, {0, 10, -1}}});
    const auto definition =
        bw::formats::CollisionDefinition::parse(wall, domain);
    collision.add(definition, bw::scene::CollisionSourceMode::character);
    bw::game::PlayerCamera camera(settings, {{0, 0, 0}, 0}, collision);
    require(close(camera.view().pose.eye[2], -.95F),
            "Initial collision shortens the rear offset");
    camera.advance({{100, 0, 0}, 0}, {}, .1);
    const float recovered_rear = -.95F - .5F * 3 / std::hypot(.5F, 3.0F);
    require(close(camera.view().pose.eye[0], 100.5F) &&
                close(camera.view().pose.eye[2], recovered_rear),
            "Leaving an obstacle restores retained offset at authored speed");
    camera.advance({{100, 0, 0}, 0}, {}, 1);
    require(close(camera.view().pose.eye[2], -3),
            "Offset recovery caps at the authored distance");
    const auto live = domain->snapshot().live_bytes;
    for (int frame = 0; frame < 120; ++frame) {
      camera.advance({{100, 0, 0}, 0}, {.5F, .1F}, 1.0 / 60);
      (void)bw::render::make_view_transforms(camera.view().pose,
                                             camera.view().perspective);
    }
    require(domain->snapshot().live_bytes == live,
            "Camera advancement retains fixed engine memory");
  }
  require(domain->snapshot().live_bytes == 0,
          "Camera settings and collision release their owned metadata");
}
