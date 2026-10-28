// Actual-provider scene bank admission, preserved surface metadata and physical
// contacts. Synthetic authored collision separates query-only and physics-only
// geometry; no rendered mesh or native launch participates.
#include "game/world/world_physics.hpp"

#include "../../../fixtures/camera_fixture.hpp"
#include "game/actors/character_body_radius.hpp"

namespace {
using camera_fixture::require;
void scenario(const std::shared_ptr<bw::memory::Domain>& domain) {
  constexpr std::array<std::array<float, 3>, 3> floor{
      {{-10, 0, -10}, {0, 0, 10}, {10, 0, -10}}};
  auto ordinary = camera_fixture::collision("floor", 4, floor);
  ordinary.at(203) = 9;
  const auto query_bytes = camera_fixture::collision(
      "ceiling", 0x64, {{{-10, 3, -10}, {0, 3, 10}, {10, 3, -10}}});
  const auto physical_bytes = camera_fixture::collision(
      "hp_hidden", 0x24, {{{20, 0, -10}, {30, 0, 10}, {40, 0, -10}}});
  const auto base = bw::formats::CollisionDefinition::parse(ordinary, domain);
  const auto ceiling =
      bw::formats::CollisionDefinition::parse(query_bytes, domain);
  const auto hidden =
      bw::formats::CollisionDefinition::parse(physical_bytes, domain);
  bw::scene::StaticCollision geometry(domain);
  geometry.add(base, bw::scene::CollisionSourceMode::character);
  geometry.add(base, bw::scene::CollisionSourceMode::environment);
  geometry.add(ceiling, bw::scene::CollisionSourceMode::environment);
  geometry.add(hidden, bw::scene::CollisionSourceMode::character);
  geometry.prepare_character_queries();
  require(geometry.part_count() == 3 && geometry.geometry_count() == 4,
          "Physics-only part changed accepted query registration");
  bool physical_only = false;
  for (std::size_t i = 0; i < geometry.geometry_count(); ++i) {
    const auto record = geometry.geometry(i);
    if (!record.query_geometry) {
      physical_only = record.physics_material == 0;
    }
  }
  require(physical_only, "Terrain material confused with collision group");
  // Common publication and custom support must consume the same source death
  // radius. Otherwise the finished animation is buried before Jolt takes it.
  const float living_radius = bw::game::character_body_radius(.25F, true);
  const float dead_radius = bw::game::character_body_radius(.25F, false);
  const auto living =
      geometry.move_character({.previous_position = {0, .26F, 0},
                               .proposed_position = {0, .26F, 0},
                               .radius = living_radius,
                               .cylinder_height = 1.8F - 2 * living_radius,
                               .delta_seconds = 1.F / 60,
                               .previously_grounded = true});
  const auto dead =
      geometry.move_character({.previous_position = living.position,
                               .proposed_position = living.position,
                               .radius = dead_radius,
                               .cylinder_height = 1.8F - 2 * dead_radius,
                               .delta_seconds = 1.F / 60,
                               .previously_grounded = true});
  require(dead.grounded && dead.position[1] > .5F &&
              std::abs((dead.position[1] - dead_radius) -
                       (living.position[1] - living_radius)) < .001F,
          "Death contact did not preserve the model above its ground plane");
  require(bw::game::character_body_radius(1.F, false) == 2.F &&
              bw::game::character_body_radius(1.01F, false) == 1.01F,
          "Source small-body death boundary changed");
  bw::game::ScenePhysicsRecipe recipe;
  recipe.gravity = {0, -9.81F, 0};
  recipe.materials[1] = {
      .restitution = .2F, .static_friction = .6F, .dynamic_friction = .6F};
  bw::physics::Configuration configuration;
  configuration.limits = {32, 32, 8, 128, 128, 64, 1024 * 1024};
  bw::world::WorldClock clock;
  bw::game::WorldPhysics world(clock, geometry, recipe, configuration, domain);
  const auto queries = world.queries();
  const auto hit =
      queries.ray({{0, 2, 0}, {0, -1, 0}, world.scenery_layers(), {}});
  require(hit && hit->surface == 9, "Scene query lost authored face surface");
  if (!hit) {
    throw std::runtime_error("Missing scene ray hit");
  }
  const auto receiver = world.receiver(hit->receiver);
  require(
      receiver && receiver->kind == 4 &&
          receiver->source_bank == bw::scene::CollisionSourceMode::environment,
      "Scene query receiver metadata lost source bank");
  require(!queries.ray({{30, 2, 0}, {30, -1, 0}, world.scenery_layers(), {}}),
          "Physics-only terrain leaked into source query bank");
  const auto resources = world.resources();
  const auto sphere = resources.create_shape(bw::physics::Sphere{.25F});
  bw::physics::BodyDescription body;
  body.shape = sphere;
  body.pose.position = {0, 5, 0};
  body.motion = bw::physics::Motion::dynamic;
  body.layer = bw::game::physical_character_layer;
  body.mass = {1, {}, bw::physics::Inertia::from_shape};
  body.material = world.character_material();
  const auto falling = resources.create_body(body);
  body.pose.position = {30, 2, 0};
  const auto hidden_floor = resources.create_body(body);
  world.advance(clock.publish(1.0F, {.simulation = false}));
  require(queries.body(falling).pose.position[1] == 5,
          "Paused world advanced rigid bodies");
  for (int i = 0; i < 240; ++i) {
    world.advance(clock.publish(1.0F / 60, {}));
  }
  const auto height = queries.body(falling).pose.position[1];
  const auto hidden_height = queries.body(hidden_floor).pose.position[1];
  require(height > .2F && height < .3F,
          "Query-only ceiling blocked body or terrain contact was absent");
  require(hidden_height > .2F && hidden_height < .3F,
          "Source physics-only terrain contact absent");
}
}  // namespace
void world_physics_tests() {
  auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  scenario(domain);
  require(domain->snapshot().live_allocations == 0,
          "World physics retained provider allocations");
}
