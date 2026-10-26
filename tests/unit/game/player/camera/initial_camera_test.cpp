// Verify authored initial-preset composition independently of WorldRuntime
// and mission scripts. Synthetic cases prove yaw and ordered collision offsets;
// no actor execution or live camera behavior is claimed. Both MCP phases: R.2
// (#rr-use-ptr), MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md).
#include "game/player/camera/initial_camera.hpp"

#include <cmath>
#include <stdexcept>

#include "../../../../fixtures/camera_fixture.hpp"
#include "game/world/initial_placement_shim.hpp"
void initial_camera_tests() {
  using camera_fixture::Backing;
  using camera_fixture::collision;
  using camera_fixture::customization;
  using camera_fixture::require;
  const auto shim =
      bw::game::initial_placement_shim("cm01_00", "st04_c", false);
  require(shim.position == std::array<float, 3>{-61.91F, .65F, -97.57F} &&
              shim.yaw_degrees == -.28F,
          "Explicit shim preserves fresh start");
  for (const auto& identity :
       {std::pair{"other", "st04_c"}, std::pair{"cm01_00", "other"}}) {
    bool rejected = false;
    try {
      (void)bw::game::initial_placement_shim(identity.first, identity.second,
                                             false);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "Shim rejects unsupported mission/stage");
  }
  bool checkpoint_rejected = false;
  try {
    (void)bw::game::initial_placement_shim("cm01_00", "st04_c", true);
  } catch (const std::invalid_argument&) {
    checkpoint_rejected = true;
  }
  require(checkpoint_rejected, "Initial shim cannot restore a checkpoint");
  const auto domain = bw::memory::Domain::create(std::make_shared<Backing>());
  {
    const auto bytes = customization();
    const auto tree =
        bw::formats::CustomizationDefinition::parse(bytes, domain);
    const auto settings =
        bw::formats::PlayerCameraSettings::parse_initial(tree);
    bw::scene::StaticCollision collision_query(domain);
    const auto camera = bw::game::make_initial_player_camera(
        {{10, 2, 20}, 90}, settings, collision_query);
    require(std::abs(camera.pose.eye[0] - 7) < .000001F &&
                std::abs(camera.pose.eye[1] - 2.7F) < .000001F &&
                std::abs(camera.pose.eye[2] - 19.5F) < .000001F,
            "Yaw rotates authored shoulder and rear offset");
    require(std::abs(camera.pose.forward[0] - 1) < .000001F &&
                camera.perspective.aspect == 16.0F / 9.0F,
            "Camera retains authored yaw and original aspect");
    const auto wall =
        collision("wall", 0x64, {{{-10, -10, -1}, {10, -10, -1}, {0, 10, -1}}});
    const auto definition =
        bw::formats::CollisionDefinition::parse(wall, domain);
    collision_query.add(definition, bw::scene::CollisionSourceMode::character);
    const auto clipped = bw::game::make_initial_player_camera(
        {{0, 0, 0}, 0}, settings, collision_query);
    require(std::abs(clipped.pose.eye[0] - .5F) < .000001F &&
                std::abs(clipped.pose.eye[1] - .7F) < .000001F &&
                std::abs(clipped.pose.eye[2] + .95F) < .000001F,
            "Rear clearance preserves previously resolved shoulder");
  }
  require(domain->snapshot().live_bytes == 0,
          "Initial camera retains no hidden allocations");
}
