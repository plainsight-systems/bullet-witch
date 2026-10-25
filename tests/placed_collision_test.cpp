// Exercise typed authored physics shapes, matrix convention, convex hull
// containment and analytic round geometry through public scene queries.
// All bytes are synthetic. Both MCP phases: I.1 (ri-explicit) checks decoder
// and registration contracts; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) checks setup
// lifetimes.
#include "game/placed_collision.hpp"

#include <cmath>

#include "camera_fixture.hpp"
#include "game/placed_lighting.hpp"
#include "model_fixture.hpp"
namespace {
using camera_fixture::require;
using camera_fixture::scalar;
using camera_fixture::word;
constexpr bw::formats::PlacementMatrix kIdentity{
    {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
bw::resources::Bytes shape_bytes(
    std::uint32_t kind, std::span<const std::array<float, 3>> points = {}) {
  bw::resources::Bytes bytes(216 + points.size() * 12);
  word(bytes, 8, 0x20060411);
  word(bytes, 16, 1);
  word(bytes, 20, 56);
  word(bytes, 56, 64);
  word(bytes, 84, 1);
  word(bytes, 88, 96);
  word(bytes, 96, 112);
  word(bytes, 112, kind);
  scalar(bytes, 128, 1);
  scalar(bytes, 144, 1);
  scalar(bytes, 160, 1);
  if (kind == 3) {
    word(bytes, 0, 192);
    word(bytes, 192, 1);
    word(bytes, 196, 8);
    word(bytes, 204, static_cast<std::uint32_t>(points.size()));
    word(bytes, 208, 16);
    for (std::size_t i = 0; i < points.size(); ++i) {
      for (std::size_t axis = 0; axis < 3; ++axis) {
        scalar(bytes, 216 + i * 12 + axis * 4, points[i].at(axis));
      }
    }
  } else {
    scalar(bytes, 176, 1);
    scalar(bytes, 180, 2);
    scalar(bytes, 184, 3);
  }
  return bytes;
}
float rear_hit(const bw::scene::StaticCollision& scene,
               std::array<float, 3> from = {0, 0, -3},
               std::array<float, 3> to = {0, 0, 0}) {
  const auto hit = scene.clip_segment({from, to, .05F, 0});
  require(hit.clipped,
          "Authored placed shape participates in camera collision");
  return hit.position[2];
}
void box_pose(const std::shared_ptr<bw::memory::Domain>& domain) {
  auto bytes = shape_bytes(1);
  // SDK packed rotation is the transpose of the engine's row-vector yaw.
  constexpr std::array<float, 9> rotation{0, 0, 1, 0, 1, 0, -1, 0, 0};
  for (std::size_t i = 0; i < rotation.size(); ++i) {
    scalar(bytes, 128 + i * 4, rotation.at(i));
  }
  scalar(bytes, 164, 2);
  const auto definition =
      bw::formats::PhysicsCollisionDefinition::parse(bytes, domain);
  require(definition.shapes().size() == 1 &&
              definition.shapes()[0].local_pose[0][2] == -1 &&
              definition.shapes()[0].local_pose[3][0] == 2,
          "Physics local pose expands the original transposed rotation");
  bw::game::PreparedPropCollision prepared(definition, domain);
  require(prepared.triangle_count() == 12 && prepared.capsule_count() == 0,
          "Authored box becomes twelve outward triangles");
  bw::formats::PlacementMatrix body{
      {{0, 0, -1, 0}, {0, 1, 0, 0}, {1, 0, 0, 0}, {10, 0, 0, 1}}};
  bw::scene::StaticCollision scene(domain);
  prepared.register_instance(std::span(&body, 1), scene);
  scene.prepare_character_queries();
  require(std::abs(rear_hit(scene, {10, 0, -8}, {10, 0, -2}) + 5.05F) < .00001F,
          "Shape-local pose composes before authored body-world pose");
}
void convex_and_round(const std::shared_ptr<bw::memory::Domain>& domain) {
  constexpr std::array<std::array<float, 3>, 10> cloud{{{-1, -1, -1},
                                                        {1, -1, -1},
                                                        {-1, 1, -1},
                                                        {1, 1, -1},
                                                        {-1, -1, 1},
                                                        {1, -1, 1},
                                                        {-1, 1, 1},
                                                        {1, 1, 1},
                                                        {0, 0, 0},
                                                        {1, 1, 1}}};
  auto bytes = shape_bytes(3, cloud);
  auto definition =
      bw::formats::PhysicsCollisionDefinition::parse(bytes, domain);
  bw::game::PreparedPropCollision convex(definition, domain);
  require(convex.triangle_count() == 12,
          "Convex hull drops interior and duplicate points");
  bw::scene::StaticCollision scene(domain);
  convex.register_instance(std::span(&kIdentity, 1), scene);
  scene.prepare_character_queries();
  require(std::abs(rear_hit(scene) + 1.05F) < .00001F,
          "Convex cloud produces the authored solid surface");
  for (const auto kind : {0U, 2U}) {
    bytes = shape_bytes(kind);
    definition = bw::formats::PhysicsCollisionDefinition::parse(bytes, domain);
    bw::game::PreparedPropCollision round(definition, domain);
    require(round.triangle_count() == 0 && round.capsule_count() == 1,
            "Sphere and capsule retain analytic geometry");
    bw::scene::StaticCollision round_scene(domain);
    round.register_instance(std::span(&kIdentity, 1), round_scene);
    round_scene.prepare_character_queries();
    require(std::abs(rear_hit(round_scene) + 1.05F) < .00001F,
            "Analytic round primitive participates in clearance");
  }
  constexpr std::array<std::array<float, 3>, 4> flat{
      {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}};
  bytes = shape_bytes(3, flat);
  definition = bw::formats::PhysicsCollisionDefinition::parse(bytes, domain);
  bool rejected = false;
  try {
    const bw::game::PreparedPropCollision invalid(definition, domain);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected,
          "Degenerate convex cloud fails instead of inventing volume");
}
void local_light(const std::shared_ptr<bw::memory::Domain>& domain) {
  bw::resources::Bytes bytes(256);
  word(bytes, 8, 0x20060411);
  word(bytes, 16, 1);
  word(bytes, 20, 56);
  word(bytes, 56, 64);
  word(bytes, 72, 3);
  word(bytes, 32, 128);
  word(bytes, 128, 0x200);
  word(bytes, 164, 3);
  scalar(bytes, 168, 1.72F);
  scalar(bytes, 180, 170);
  scalar(bytes, 192, 1.25F);
  scalar(bytes, 196, 1);
  auto definition =
      bw::formats::PhysicsCollisionDefinition::parse(bytes, domain);
  auto body = kIdentity;
  body[3] = {11, 12, 13, 1};
  const auto before = domain->snapshot().live_allocations;
  const auto light = bw::game::prepare_placed_light(
      definition, std::span(&body, 1), &kIdentity, 4);
  require(light && light->position == std::array<float, 3>{11, 12, 13} &&
              light->diffuse[0] == 1.72F && light->specular[0] == 170 &&
              light->attenuation == std::array<float, 3>{1.25F, 1, 0},
          "Emitter uses authored joint world pose and independent colors");
  require(domain->snapshot().live_allocations == before,
          "Placed light publication retains no allocation");
  const auto fallback = bw::game::prepare_placed_light(
      definition, std::span(&body, 1), &kIdentity, 3);
  require(fallback && fallback->position == std::array<float, 3>{},
          "Out-of-range source joint uses explicit owner transform");
  word(bytes, 72, 2);
  definition = bw::formats::PhysicsCollisionDefinition::parse(bytes, domain);
  bool rejected = false;
  try {
    (void)bw::game::prepare_placed_light(definition, std::span(&body, 1),
                                         &kIdentity, 4);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Valid joint without a body publication is not invented");
  word(bytes, 128, 0);
  definition = bw::formats::PhysicsCollisionDefinition::parse(bytes, domain);
  require(!bw::game::prepare_placed_light(definition, {}, nullptr, 0),
          "Absent emitter does not manufacture a scene light");
  word(bytes, 128, 0x200);
  word(bytes, 168, 0x7f800000);
  rejected = false;
  try {
    (void)bw::formats::PhysicsCollisionDefinition::parse(bytes, domain);
  } catch (const bw::resources::Error&) {
    rejected = true;
  }
  require(rejected, "Nonfinite enabled light fails bounded decoding");
}
// Slot zero is an authored control point, not skeleton joint zero. Exercise
// its selected record independently of unrelated slots and the emitter path.
void receiver_position(const std::shared_ptr<bw::memory::Domain>& domain) {
  const auto encoded = model_fixture::archive({});
  bw::resources::ArchiveBytes owned(
      encoded.begin(), encoded.end(),
      bw::memory::Allocator<std::uint8_t>(domain));
  const auto definition = bw::formats::ModelDefinition::parse(
      bw::resources::ResourceArchive::parse(std::move(owned)), domain);
  constexpr std::array<std::int32_t, 2> joints{2, 5};
  std::array<bw::formats::PlacementMatrix, 2> matrices{kIdentity, kIdentity};
  matrices[0][3] = {10, 20, 30, 1};
  matrices[1] = {{{0, 1, 0, 0}, {-1, 0, 0, 0}, {0, 0, 1, 0}, {-3, 4, 7, 1}}};
  bw::game::ResidentModelView view{"fixture", definition, {}, joints};
  const auto fallback = std::array<float, 3>{10, 20, 30};
  require(bw::game::placed_receiver_position(view, matrices) == fallback,
          "Missing control resource uses first placement, without joint zero");
  bw::resources::Bytes bytes(80);
  word(bytes, 0, 0x4250435f);
  word(bytes, 4, 1);
  word(bytes, 8, 2);
  word(bytes, 12, 20);
  word(bytes, 16, 0xffffffff);
  word(bytes, 20, 3);
  word(bytes, 24, 2);
  scalar(bytes, 28, 1);
  scalar(bytes, 32, 2);
  scalar(bytes, 36, 3);
  word(bytes, 64, 2);
  word(bytes, 68, 5);
  scalar(bytes, 72, .25F);
  scalar(bytes, 76, .75F);
  view.control_points = bytes;
  const auto before = domain->snapshot().live_allocations;
  require(bw::game::placed_receiver_position(view, matrices) ==
              std::array<float, 3>{-1, 9.25F, 15.75F},
          "Selected point uses weighted direct world poses once; ignores other "
          "slots");
  require(domain->snapshot().live_allocations == before,
          "Placed receiver query retains no allocation");
  scalar(bytes, 76, .5F);
  require(bw::game::placed_receiver_position(view, matrices) ==
              std::array<float, 3>{.25F, 8, 13.25F},
          "Control-point weights remain unnormalized");
  word(bytes, 24, 0);
  require(bw::game::placed_receiver_position(view, matrices) ==
              std::array<float, 3>{1, 2, 3},
          "No influences retains authored origin without another placement");
  word(bytes, 24, 2);
  word(bytes, 64, 0xffffffff);
  require(bw::game::placed_receiver_position(view, matrices) == fallback,
          "Source signed minus-one influence returns owner fallback");
  word(bytes, 12, 0);
  require(bw::game::placed_receiver_position(view, matrices) == fallback,
          "Absent selected slot uses owner fallback");
  word(bytes, 12, 20);
  auto rejects = [&](bw::resources::ErrorCode code) {
    bool rejected = false;
    try {
      (void)bw::game::placed_receiver_position(view, matrices);
    } catch (const bw::resources::Error& error) {
      rejected = error.code == code;
    }
    require(rejected,
            "Invalid selected control data rejects before publication");
  };
  word(bytes, 64, 6);
  rejects(bw::resources::ErrorCode::unsupported);
  word(bytes, 64, 2);
  word(bytes, 28, 0x7f800000);
  rejects(bw::resources::ErrorCode::malformed);
  scalar(bytes, 28, 1);
  view.control_points = std::span(bytes).first(79);
  rejects(bw::resources::ErrorCode::malformed);
  view.control_points = bytes;
  view.body_joints = std::span(joints).first(1);
  rejects(bw::resources::ErrorCode::malformed);
}
}  // namespace
void placed_collision_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  box_pose(domain);
  convex_and_round(domain);
  local_light(domain);
  receiver_position(domain);
  require(domain->snapshot().live_bytes == 0,
          "Prepared prop geometry releases injected storage");
}
