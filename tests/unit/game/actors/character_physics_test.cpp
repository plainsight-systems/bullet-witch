// Actual-provider handoff and hierarchy reconstruction with a tiny authored
// two-bone skeleton; no original assets or generated fixtures.
#include "game/actors/character_physics.hpp"

#include <cmath>
#include <memory_resource>
#include <stdexcept>

#include "../../../fixtures/model_fixture.hpp"
#include "bw/math/quaternion.hpp"
#include "bw/math/rigid_transform.hpp"

namespace {
class Backing final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    try {
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* value, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(value, bytes, alignment);
  }
};
void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}
void scalar(bw::resources::Bytes& bytes, std::size_t offset, float value) {
  model_fixture::word(bytes, offset, std::bit_cast<std::uint32_t>(value));
}
bw::resources::Bytes skeleton_bytes() {
  bw::resources::Bytes bytes(216);
  model_fixture::word(bytes, 0, 0x424c425f);
  model_fixture::word(bytes, 4, 2);
  model_fixture::word(bytes, 12, 1);
  model_fixture::word(bytes, 116, 0xffffffff);
  for (auto bone : {8U, 112U}) {
    for (auto transform : {24U, 64U}) {
      scalar(bytes, bone + transform + 12, 1);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        scalar(bytes, bone + transform + 28 + axis * 4, 1);
      }
    }
  }
  scalar(bytes, 8 + 24 + 16, 1);
  scalar(bytes, 8 + 64 + 16, 1);
  return bytes;
}
}  // namespace
void character_physics_tests() {
  // Stable quaternion extraction checks every dominant diagonal and trace case.
  for (const bw::math::Vector<float, 4> q :
       std::array<bw::math::Vector<float, 4>, 5>{{{0, 0, 0, 1},
                                                  {1, 0, 0, 0},
                                                  {0, 1, 0, 0},
                                                  {0, 0, 1, 0},
                                                  {.5F, .5F, .5F, .5F}}}) {
    const auto matrix = bw::math::homogeneous_transform(
        bw::math::TransformComponents<float>{q, {}, {1, 1, 1}});
    const auto back = bw::math::rigid_rotation(matrix, .0001F);
    if (!back) {
      throw std::runtime_error("Rigid quaternion extraction rejected rotation");
    }
    require(
        std::abs(std::abs(bw::math::accumulated_dot(q, *back)) - 1) < .0001F,
        "Rigid rotation convention mismatch");
  }
  auto scaled = bw::math::identity_matrix<float, 4>();
  scaled[0][0] = 2;
  require(!bw::math::rigid_rotation(scaled, .0001F),
          "Scale was silently accepted as a rigid rotation");
  const auto decomposed = bw::math::decompose_orthogonal_affine(scaled, .0001F);
  require(
      decomposed && decomposed->scale[0] == 2 && decomposed->rotation[3] == 1,
      "Physical decomposition must return scale explicitly");
  scaled[1][0] = .5F;
  require(!bw::math::decompose_orthogonal_affine(scaled, .0001F),
          "Physical decomposition silently discarded shear");
  auto domain = bw::memory::Domain::create(std::make_shared<Backing>());
  {
    const auto skeleton =
        bw::formats::SkeletonDefinition::parse(skeleton_bytes(), domain);
    bw::animation::SkeletonPose pose(skeleton, domain);
    auto root = bw::animation::kIdentityMatrix;
    root[3][1] = 5;
    root[0][0] = root[1][1] = root[2][2] = .999F;
    pose.bind_pose(root);
    const std::array<bw::formats::ModelBoneName, 2> names{
        {{"child", 0}, {"root", 1}}};
    bw::game::CharacterPhysicsRecipe recipe;
    recipe.body_count = 2;
    recipe.joint_count = 1;
    for (std::size_t i = 0; i < 2; ++i) {
      auto& body = recipe.bodies.at(i);
      body.geometry = bw::game::CharacterPhysicsBody::Geometry::box;
      body.half_extent = {.2F, .2F, .2F};
      body.mass = 1;
      const std::string_view name = i == 0 ? "root" : "child";
      std::ranges::copy(name, body.bone.begin());
    }
    recipe.joints[0] = {.first = 0,
                        .second = 1,
                        .axis = {0, 0, 1},
                        .minimum_angle = -.5F,
                        .maximum_angle = .5F,
                        .kind = bw::game::CharacterPhysicsJoint::Kind::hinge};
    bw::physics::Runtime runtime(domain);
    bw::world::WorldClock clock;
    bw::physics::Configuration config;
    config.collision_masks[0] = 1;
    config.limits = {.bodies = 8,
                     .shapes = 8,
                     .constraints = 4,
                     .body_pairs = 32,
                     .contacts = 32,
                     .commands = 32,
                     .scratch_bytes = 1024 * 1024};
    auto world = runtime.create_world(config, clock);
    {
      bw::game::CharacterPhysics actor(world.resources(), world.queries(),
                                       world.mutations(), recipe, skeleton,
                                       names, pose, {}, domain);
      require(std::abs(actor.animation_scales()[0][0] - .999F) < .00001F,
              "Character body failed to retain extracted animation scale");
      root[3][1] = 6;
      pose.bind_pose(root);
      actor.follow_animation(pose);
      require(!actor.receive_corpse_shot({1, 0, 0}, {0, 6, 0}),
              "Living animation body received corpse impulse");
      world.advance(clock.publish(1.F / 60, {}));
      require(actor.request_impulse({{1, 0, 0}, {0, 6, 0}, 1, 2, 0}),
              "Initial pending impulse was rejected");
      require(!actor.request_impulse({{-1, 0, 0}, {0, 6, 0}, 10, 1, 0}),
              "Lower-priority force replaced the retained request");
      require(!actor.request_impulse({{-1, 0, 0}, {0, 6, 0}, .5F, 2, 0}),
              "Weaker equal-priority force replaced the retained request");
      actor.handoff();
      world.advance(clock.publish(1.F / 60, {}));
      const auto model = actor.publish(pose);
      require(actor.physical() && model[3][0] > 0,
              "Physical handoff did not publish displaced root");
      require(
          pose.local_transforms()[1].translation == bw::math::Vector3<float>{},
          "Pelvis local was not cleared");
      require(pose.local_transforms()[0].translation ==
                  skeleton.bones()[0].relative.translation,
              "Physics replaced authored child translation");
      const auto momentum_x = [&] {
        float sum = 0;
        for (const auto body : actor.bodies()) {
          sum += world.queries().body(body).linear_velocity[0];
        }
        return sum;
      };
      const float before_shot = momentum_x();
      require(actor.receive_corpse_shot({1, 0, 0}, {0, 6, 0}),
              "Physical corpse rejected ordinary hit impulse");
      require(momentum_x() == before_shot,
              "Corpse receipt integrated outside world advance");
      world.advance(clock.publish(1.F / 60, {}));
      require(momentum_x() > before_shot + 50,
              "Source corpse impulse did not reach the provider");
      const auto bone_world = pose.bone_transforms();
      require(bw::math::length(bw::math::subtract(
                  bw::math::translation(bone_world[0]),
                  bw::math::translation(bone_world[1]))) > .9F,
              "Hierarchy collapsed during physical publication");
    }
    require(world.body_count() == 0, "Character teardown retained bodies");
    // Joint zero comes from bind, not whichever animation was active at load.
    std::array<bw::formats::SkeletonTransform, 2> initial{
        skeleton.bones()[0].relative, skeleton.bones()[1].relative};
    initial[0].rotation = {0, 0, std::sin(.6F), std::cos(.6F)};
    pose.evaluate_locals(initial, root);
    {
      bw::game::CharacterPhysics actor(world.resources(), world.queries(),
                                       world.mutations(), recipe, skeleton,
                                       names, pose, {}, domain);
      actor.handoff();
      for (int i = 0; i < 60; ++i) {
        world.advance(clock.publish(1.F / 60, {}));
      }
      const auto first = world.queries().body(actor.bodies()[0]).pose.rotation;
      const auto second = world.queries().body(actor.bodies()[1]).pose.rotation;
      const auto relative =
          bw::math::quaternion_product(bw::math::QuaternionProduct<float>{
              bw::math::quaternion_inverse_or_zero(first, 0.F), second});
      require(std::abs(2 * std::atan2(relative[2], relative[3])) < .65F,
              "Spawn animation replaced authored hinge rest pose");
    }
    recipe.joints[0].second = 0;
    bool rejected = false;
    try {
      bw::game::CharacterPhysics invalid(world.resources(), world.queries(),
                                         world.mutations(), recipe, skeleton,
                                         names, pose, {}, domain);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected && world.body_count() == 0,
            "Failed joint setup did not roll back bodies and shapes");
  }
  require(domain->snapshot().live_allocations == 0,
          "Character physics retained engine storage");
}
