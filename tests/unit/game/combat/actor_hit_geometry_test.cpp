// Bone-bound receiver tests use the real physics provider. Synthetic triangles
// isolate group selection, pose ownership and packed face metadata, not parity.
#include "game/combat/actor_hit_geometry.hpp"

#include <bit>
#include <cmath>

#include "../../../fixtures/camera_fixture.hpp"
void actor_hit_geometry_tests() {
  namespace physics = bw::physics;
  namespace math = bw::math;
  namespace memory = bw::memory;
  namespace formats = bw::formats;
  namespace animation = bw::animation;
  namespace game = bw::game;
  using camera_fixture::require;
  const auto domain =
      memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  physics::Runtime runtime(domain);
  bw::world::WorldClock clock;
  physics::Configuration config;
  config.limits = {.bodies = 8,
                   .shapes = 4,
                   .constraints = 1,
                   .body_pairs = 16,
                   .contacts = 16,
                   .commands = 8,
                   .scratch_bytes = 1024 * 1024,
                   .query_instances = 8};
  auto world = runtime.create_world(config, clock);
  auto bytes = camera_fixture::collision("head", 5U | (4U << 8U));
  camera_fixture::word(bytes, 64 + 60, 1);
  bytes.at(202) = 3;
  bytes.at(203) = 7;
  const auto definition = formats::CollisionDefinition::parse(bytes, domain);
  auto geometry = std::allocate_shared<game::ActorHitGeometry>(
      memory::Allocator<game::ActorHitGeometry>(domain), world.resources(),
      definition, domain);
  // EHIT vertices are model-bind coordinates. A translated authored bone must
  // cancel its bind transform before placing these query triangles in world.
  bw::resources::Bytes skeleton_bytes(216);
  camera_fixture::word(skeleton_bytes, 0, 0x424c425f);
  camera_fixture::word(skeleton_bytes, 4, 2);
  const auto scalar = [&](std::size_t offset, float value) {
    camera_fixture::word(skeleton_bytes, offset,
                         std::bit_cast<std::uint32_t>(value));
  };
  for (const std::size_t bone : {8U, 112U}) {
    camera_fixture::word(skeleton_bytes, bone + 4, 0xffffffff);
    for (const std::size_t transform : {24U, 64U}) {
      scalar(bone + transform + 12, 1);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        scalar(bone + transform + 28 + axis * 4, 1);
      }
      if (bone == 112) scalar(bone + transform + 20, 3);
    }
  }
  const auto skeleton =
      formats::SkeletonDefinition::parse(skeleton_bytes, domain);
  animation::SkeletonPose pose(skeleton, domain);
  auto model = animation::kIdentityMatrix;
  pose.bind_pose(model);
  std::array<std::uint32_t, 8> groups{};
  {
    game::ActorHitInstance hidden(world.resources(), world.mutations(),
                                  geometry, groups, pose, 41, 1, domain);
    require(world.body_count() == 0, "Hidden EHIT group has no receiver");
  }
  groups[0] = 1U << 4U;
  // Authored animation matrices retain scale; neither admission nor update
  // may normalize their basis into a rigid body rotation.
  model[0][0] = .99F;
  model[1][1] = 1.01F;
  model[2][2] = .98F;
  pose.bind_pose(model);
  {
    game::ActorHitInstance actor(world.resources(), world.mutations(), geometry,
                                 groups, pose, 42, 1, domain);
    const auto hit = world.queries().ray({{0, 0, 0}, {0, 0, 2}, 2, {}});
    require(
        hit && hit->user_data == 42 && hit->surface == 0x0703 &&
            std::holds_alternative<physics::QueryInstanceId>(hit->receiver) &&
            std::abs(hit->fraction - .49F) < .00001F && world.body_count() == 0,
        "Affine EHIT preserves metadata and authored scale without solver "
        "bodies");
    require(!world.queries().ray({{0, 0, 0}, {0, 0, 2}, 2, {}, 42}),
            "Owner exclusion applies to copied actor identity");
    model[3][0] = 20;
    pose.bind_pose(model);
    actor.publish(pose);
    require(
        world.queries().ray({{0, 0, 0}, {0, 0, 2}, 2, {}}).has_value(),
        "Publishing a bone cannot mutate the physics world before its step");
    world.advance(clock.publish(.016F, {}));
    require(!world.queries().ray({{0, 0, 0}, {0, 0, 2}, 2, {}}) &&
                world.queries().ray({{20, 0, 0}, {20, 0, 2}, 2, {}}),
            "World admits receiver update from its authored bone");
    actor.retire();
    require(world.body_count() == 0,
            "Receiver retires without retiring shared shapes");
  }
  geometry.reset();
}
