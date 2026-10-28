// Persistent shots against real provider geometry: no fake ray-hit sink.
// Synthetic positions exercise source range/query scheduling, not game parity.
#include "game/combat/projectile_world.hpp"

#include "../../../fixtures/camera_fixture.hpp"
#include "game/combat/shot_target.hpp"

namespace {
using camera_fixture::require;
template <class Function>
void rejects(Function operation) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, "Invalid projectile operation admitted");
}
}  // namespace
void projectile_world_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  bw::physics::Runtime runtime(domain);
  bw::world::WorldClock clock;
  bw::physics::Configuration configuration;
  configuration.limits = {.bodies = 8,
                          .shapes = 4,
                          .constraints = 1,
                          .body_pairs = 16,
                          .contacts = 16,
                          .commands = 8,
                          .scratch_bytes = 1024 * 1024};
  auto physics = runtime.create_world(configuration, clock);
  const auto shape = physics.create_shape(bw::physics::Sphere{0.5F});
  bw::physics::BodyDescription body;
  body.shape = shape;
  body.layer = 1;
  body.pose.position = {0, 0, 3};
  body.user_data = 42;
  const auto actor = physics.create_body(body);
  body.layer = 0;
  body.pose.position = {0, 0, 5};
  body.user_data = 7;
  const auto scenery = physics.create_body(body);
  // A nearer Dodge/Blast receiver must not consume the shot or aim selection.
  // The source transient flags are distinct from the actor's query-bank layer.
  body.layer = 1;
  body.pose.position = {0, 0, 1};
  body.user_data = 43;
  body.query_flags = 0xA;
  const auto dodging_actor = physics.create_body(body);
  body.layer = 0;
  body.query_flags = 0x80000000U;
  const auto disabled_scenery = physics.create_body(body);
  const auto unfiltered_actor =
      physics.queries().ray({.start = {}, .end = {0, 0, 6}, .layers = 2});
  const auto unfiltered_scenery =
      physics.queries().ray({.start = {}, .end = {0, 0, 6}, .layers = 1});
  require(unfiltered_actor && unfiltered_scenery &&
              unfiltered_actor->receiver ==
                  bw::physics::ReceiverId{dodging_actor} &&
              unfiltered_scenery->receiver ==
                  bw::physics::ReceiverId{disabled_scenery},
          "Filter regression fixtures must precede ordinary receivers");
  // Camera selection uses the same provider with original scenery-first
  // ordering. Actor eligibility is a bank mask, never inferred from a model.
  const bw::game::PlayerShotView view{{0, 0, -2}, {0, 0, 1}, {}, 20};
  const auto actor_target =
      bw::game::player_shot_target(physics.queries(), view, {1, 2});
  require(actor_target.hit &&
              actor_target.hit->receiver == bw::physics::ReceiverId{actor} &&
              std::abs(actor_target.position[2] - 2.5F) < .001F,
          "Camera target did not select eligible actor before scenery");
  const auto scene_target =
      bw::game::player_shot_target(physics.queries(), view, {1, 0});
  require(scene_target.hit &&
              scene_target.hit->receiver == bw::physics::ReceiverId{scenery} &&
              std::abs(scene_target.position[2] - 4.49F) < .001F,
          "Camera scenery target lost original clearance");
  const auto clear_target =
      bw::game::player_shot_target(physics.queries(), view, {0, 0});
  require(!clear_target.hit && clear_target.position[2] == 18,
          "Clear camera target did not retain far endpoint");
  bw::game::StraightWeaponDefinition gun;
  gun.range = 20;
  gun.speed = 10;
  gun.damage = {{100, 200, 300}, {.5F, .25F, .1F}, 10, 20};
  require(gun.damage.at_distance(0, 0) == 100 &&
              gun.damage.at_distance(2, 15) == 125 &&
              gun.damage.at_distance(3, 30) == 30,
          "Source damage levels and clamped distance interpolation");
  bw::game::StraightShot shot{.definition = &gun,
                              .direction = {0, 0, 1},
                              .owner = 1,
                              .level = 1,
                              .scenery_layers = 1,
                              .actor_layers = 2};
  {
    bw::game::ProjectileWorld projectiles(clock, physics.queries(), {2, 2},
                                          domain);
    auto delayed = shot;
    delayed.initial_age_seconds = .1F;
    const auto id = projectiles.emit(delayed);
    const auto first = clock.publish(.1F, {});
    projectiles.advance(first, {});
    require(projectiles.active_count() == 1 &&
                projectiles.events()[0].position[2] == 2,
            "First update consumes source firing overshoot exactly once");
    rejects([&] { projectiles.advance(first, {}); });
    projectiles.advance(clock.publish(1, {.simulation = false}), {});
    require(projectiles.events()[0].position[2] == 2 &&
                projectiles.active_count() == 1,
            "Pause retains bullet and publication");
    projectiles.advance(clock.publish(.1F, {}), {});
    const auto& impact = projectiles.events()[0];
    require(
        impact.projectile == id &&
            impact.kind == bw::game::ProjectileEventKind::impact &&
            impact.hit &&
            impact.hit->receiver == bw::physics::ReceiverId{actor} &&
            impact.hit->user_data == 42 &&
            std::abs(impact.position[2] - 2.5F) < .001F &&
            std::abs(impact.damage_distance - 3.5F) < .001F &&
            projectiles.active_count() == 0,
        "Actor impact wins before scenery and carries original falloff input");
    const auto reused = projectiles.emit(shot);
    require(reused.slot == id.slot && reused.generation != id.generation,
            "Reused projectile slot changes identity generation");
  }
  {
    bw::game::ProjectileWorld projectiles(clock, physics.queries(), {2, 1},
                                          domain);
    const auto first = projectiles.emit(shot);
    const auto second = projectiles.emit(shot);
    rejects([&] { projectiles.emit(shot); });
    projectiles.advance(clock.publish(.2F, {}), {});
    projectiles.advance(clock.publish(.2F, {}), {});
    require(projectiles.events()[0].projectile == first &&
                projectiles.events()[0].kind ==
                    bw::game::ProjectileEventKind::moved &&
                projectiles.events()[1].projectile == second &&
                projectiles.events()[1].kind ==
                    bw::game::ProjectileEventKind::impact,
            "Query priority rotates independently of update order; skipped "
            "segment accumulates");
  }
  {
    bw::game::ProjectileWorld projectiles(clock, physics.queries(), {1, 1},
                                          domain);
    auto only_scenery = shot;
    only_scenery.actor_layers = 0;
    projectiles.emit(only_scenery);
    projectiles.advance(clock.publish(.6F, {}), {});
    const auto& hit = projectiles.events()[0].hit;
    require(hit && hit->receiver == bw::physics::ReceiverId{scenery} &&
                std::abs(projectiles.events()[0].position[2] - 4.49F) < .001F,
            "Scenery receives projectile when actor bank excludes actor");
  }
  {
    bw::game::ProjectileWorld projectiles(clock, physics.queries(), {1, 1},
                                          domain);
    gun.range = 1;
    projectiles.emit(shot);
    projectiles.advance(clock.publish(.2F, {}), {});
    require(projectiles.events()[0].kind ==
                    bw::game::ProjectileEventKind::expired &&
                projectiles.events()[0].position == shot.muzzle &&
                !projectiles.events()[0].hit,
            "Range expiration does not invent a final movement or impact");
  }
}
