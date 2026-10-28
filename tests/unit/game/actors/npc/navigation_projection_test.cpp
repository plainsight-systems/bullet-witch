// Exercise sensor composition against shared collision geometry; no authored
// game data. I.1 exposes ground-miss behavior; MEM.9 fixes query storage.
#include "game/actors/npc/navigation_projection.hpp"

#include "../../../../fixtures/camera_fixture.hpp"
void navigation_projection_tests() {
  using camera_fixture::require;
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  bw::scene::StaticCollision collision(domain);
  require(bw::game::npc_ground_destination(collision, {1, 2, 3}, 0, 20) ==
              std::array<float, 3>{1, -1000000.F, 3},
          "Missing original ground retains its sentinel");
  require(
      !bw::game::npc_dodge_clearance(collision, {{0, .4F, 0}, {0, 0, 4.6F}}),
      "Dodge rejects missing ground");
  const std::array<bw::scene::CollisionTriangle, 1> floor{
      bw::scene::CollisionTriangle{
          {{{-20, 0, -20}, {0, 0, 20}, {20, 0, -20}}}}};
  collision.add_triangles(floor);
  collision.prepare_character_queries();
  require(!collision.overlaps_capsule({0, .3F, 0}, {0, .3F, 3}, .25F, 0x1000),
          "Raised source capsule clears flat floor across its entire axis");
  bw::game::NpcMovementSchedule movement_schedule;
  bw::game::NpcMovementSensor movement_sensor;
  const auto walking = movement_sensor.query(
      collision, movement_schedule, {{0, .26F, 0}, {0, .26F, 2}, .25F});
  require(walking.supported && walking.traversable,
          "Raised movement support accepts flat ground without whole-route "
          "floor blockage");
  bw::game::NpcMovementSchedule scheduling;
  require(scheduling.reserve() == 19 && scheduling.reserve() == 18,
          "Movement checks reserve the highest least-loaded future bucket");
  scheduling.advance();
  require(scheduling.reserve() == 19,
          "World phase rotates scheduled checks before new reservations");
  require(bw::game::npc_dodge_clearance(collision, {{0, .4F, 0}, {0, 0, 4.6F}}),
          "Dodge accepts a distant supported destination");
  require(!bw::game::npc_dodge_clearance(collision, {{0, .4F, 0}, {0, 0, 2}}),
          "Dodge rejects clearance shorter than original distance");
  auto result =
      bw::game::npc_retreat_destination(collision, {}, {0, 0, 3}, .4F);
  require(
      std::abs(result[1] - .4F) < .00001F && std::abs(result[2] - 3) < .00001F,
      "Retreat probes down then restores original body radius");
  const std::array<bw::scene::CollisionTriangle, 1> wall{
      bw::scene::CollisionTriangle{
          {{{-10, -10, 1}, {0, 10, 1}, {10, -10, 1}}}}};
  collision.add_triangles(wall);
  collision.prepare_character_queries();
  require(collision.overlaps_capsule({0, .3F, 0}, {0, .3F, 3}, .25F, 0x1000),
          "Arbitrary capsule detects a wall between two clear endpoints");
  bw::game::NpcMovementSensor wall_sensor;
  const auto blocked = wall_sensor.query(collision, movement_schedule,
                                         {{0, .26F, .7F}, {0, .26F, 2}, .25F});
  require(!blocked.supported && !blocked.traversable,
          "Source local sensor rejects an upright obstruction");
  const auto held = wall_sensor.query(collision, movement_schedule,
                                      {{0, .26F, -5}, {0, .26F, -3}, .25F});
  require(
      !held.traversable,
      "Repeated action calls retain sensor flags until the scheduled frame");
  for (int frame = 0; frame < 19; ++frame) {
    wall_sensor.advance();
    movement_schedule.advance();
  }
  require(
      wall_sensor
          .query(collision, movement_schedule,
                 {{0, .26F, -5}, {0, .26F, -3}, .25F})
          .traversable,
      "Due sensor refresh replaces retained obstruction from real geometry");
  bw::scene::StaticCollision owned(domain);
  owned.add_triangles(floor, {}, true);
  owned.prepare_character_queries();
  bw::game::NpcMovementSensor owned_sensor;
  require(
      !owned_sensor
           .query(owned, movement_schedule, {{0, .26F, 0}, {0, .26F, 2}, .25F})
           .supported,
      "Owned prop ground is rejected independently of its flat normal");
  require(
      !bw::game::npc_dodge_clearance(collision, {{0, .4F, 0}, {0, 0, 4.6F}}),
      "Dodge rejects a blocking upright surface");
  result = bw::game::npc_retreat_destination(collision, {}, {0, 0, 3}, .4F);
  require(std::abs(result[2] - .6F) < .00001F &&
              std::abs(result[1] - .4F) < .00001F,
          "Retreat clears the wall before probing its corrected destination");
}
