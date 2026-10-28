// Actual provider contract checks: ownership, filtered mesh queries, queued
// kinematic/dynamic handoff, pause, constrained motion and complete retirement.
#include "bw/physics/world.hpp"

#include <cmath>
#include <memory_resource>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template <class Function>
void rejects(Function operation) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, "Invalid physics operation was admitted");
}
class TestBacking final : public bw::memory::CpuBacking {
 public:
  std::size_t allocations{};
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    ++allocations;
    try {
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
};
bw::physics::Hit required_hit(const bw::physics::Queries& queries,
                              const bw::physics::Query& query) {
  const auto hit = queries.ray(query);
  if (!hit) throw std::runtime_error("Expected query hit");
  return *hit;
}
void affine_queries(bw::physics::Runtime& runtime, const TestBacking& backing) {
  namespace physics = bw::physics;
  namespace math = bw::math;
  bw::world::WorldClock clock;
  physics::Configuration config;
  config.limits = {.bodies = 2,
                   .shapes = 4,
                   .constraints = 1,
                   .body_pairs = 4,
                   .contacts = 4,
                   .commands = 4,
                   .scratch_bytes = 1024 * 1024,
                   .query_instances = 2};
  auto world = runtime.create_world(config, clock);
  constexpr std::array<physics::Vector3, 4> points{
      {{-10, 0, -10}, {10, 0, -10}, {10, 0, 10}, {-10, 0, 10}}};
  constexpr std::array<physics::Triangle, 2> triangles{
      {{{0, 2, 1}, 17}, {{0, 3, 2}, 23}}};
  const auto mesh =
      world.create_shape(physics::TriangleMesh{points, triangles});
  physics::BodyDescription body;
  body.shape = mesh;
  const auto floor = world.create_body(body);
  physics::QueryInstanceDescription description;
  description.shape = mesh;
  description.transform = {{{2, .5F, 0}, {.2F, 1, 0}, {0, .3F, 1}, {0, 2, 0}}};
  description.user_data = 42;
  description.query_flags = 8;
  const auto instance = world.create_query_instance(description);
  const auto queries = world.queries();
  const auto mutations = world.mutations();
  const physics::Query ray{.start = {0, 4, 0}, .end = {0, -2, 0}};
  const auto allocation_count = backing.allocations;
  const auto hit = queries.ray(ray);
  const auto expected_normal =
      math::divide(physics::Vector3{-.5F, 2, -.6F},
                   math::length(physics::Vector3{-.5F, 2, -.6F}));
  require(
      hit && hit->receiver == physics::ReceiverId{instance} &&
          hit->user_data == 42 && std::abs(hit->fraction - 1.0F / 3) < 1.e-5F &&
          math::length(math::subtract(hit->normal, expected_normal)) < 1.e-5F,
      "Affine query lost shear, fraction, identity or inverse-transpose "
      "normal");
  std::array<physics::SphereOverlap, 8> contacts{};
  physics::SphereQuery sphere_query{
      .center = math::add(physics::Vector3{0, 2, 0},
                          math::scale(expected_normal, .5F)),
      .radius = 1};
  const auto overlaps = queries.overlap_sphere(sphere_query, contacts);
  require(overlaps.written == 2 && !overlaps.truncated,
          "Affine sphere query must retain both overlapping triangle faces");
  for (std::size_t index = 0; index < overlaps.written; ++index) {
    const auto& contact = contacts.at(index);
    require(contact.receiver == physics::ReceiverId{instance} &&
                contact.shape == mesh && contact.user_data == 42 &&
                std::abs(contact.separation + .5F) < 1.e-4F &&
                math::length(contact.position) > 1.9F &&
                math::length(math::subtract(
                    contact.position, physics::Vector3{0, 2, 0})) < 1.e-4F,
            "Affine sphere contact must use world distance and closest point");
  }
  auto filtered_sphere = sphere_query;
  filtered_sphere.exclude_flags = 8;
  require(queries.overlap_sphere(filtered_sphere, contacts).written == 0,
          "Sphere query ignored receiver flags");
  filtered_sphere = sphere_query;
  filtered_sphere.exclude_user_data = 42;
  require(queries.overlap_sphere(filtered_sphere, contacts).written == 0,
          "Sphere query ignored receiver identity");
  filtered_sphere = sphere_query;
  filtered_sphere.exclude_instance = instance;
  require(queries.overlap_sphere(filtered_sphere, contacts).written == 0,
          "Sphere query ignored typed instance exclusion");
  filtered_sphere = sphere_query;
  filtered_sphere.layers = 0;
  require(queries.overlap_sphere(filtered_sphere, contacts).written == 0,
          "Sphere query ignored layer filtering");
  require(backing.allocations == allocation_count,
          "Affine sphere narrowphase allocated during query");
  auto excluded = ray;
  excluded.exclude_flags = 8;
  const auto farther = queries.ray(excluded);
  require(farther && farther->receiver == physics::ReceiverId{floor},
          "Filtered affine instance hid farther solver geometry");
  excluded = ray;
  excluded.exclude_instance = instance;
  require(
      required_hit(queries, excluded).receiver == physics::ReceiverId{floor},
      "Typed instance exclusion failed");
  const physics::QueryFlags cleared_flags{instance, 0};
  const auto before_flags = backing.allocations;
  mutations.query_flags(std::span(&cleared_flags, 1));
  require(backing.allocations == before_flags,
          "Query flag publication allocated after admission");
  require(
      required_hit(queries, excluded).receiver == physics::ReceiverId{floor},
      "Queued flags must not mutate the active query snapshot");
  world.advance(clock.publish(.016F, {.simulation = false}));
  excluded = ray;
  excluded.exclude_flags = 8;
  require(
      required_hit(queries, excluded).receiver == physics::ReceiverId{floor},
      "Pause must retain pending query flags");
  world.advance(clock.publish(.016F, {}));
  require(
      required_hit(queries, excluded).receiver == physics::ReceiverId{instance},
      "World advance must publish source query flag changes");
  const physics::QueryFlags restored_flags{instance, 8};
  mutations.query_flags(std::span(&restored_flags, 1));
  world.advance(clock.publish(.016F, {}));
  auto moved = description.transform;
  moved[3][1] = 3;
  const auto before_mutation = backing.allocations;
  mutations.query_transform(instance, moved);
  require(backing.allocations == before_mutation,
          "Query or affine publication allocated after world setup");
  world.advance(clock.publish(.016F, {.simulation = false}));
  require(std::abs(required_hit(queries, ray).fraction - 1.0F / 3) < 1.e-5F,
          "Pause admitted affine mutation");
  world.advance(clock.publish(.016F, {}));
  require(std::abs(required_hit(queries, ray).fraction - 1.0F / 6) < 1.e-5F,
          "World did not admit queued affine transform");
  auto singular = moved;
  singular[0] = {};
  const std::array<physics::QueryTransform, 2> invalid_batch{
      {{instance, description.transform}, {instance, singular}}};
  rejects([&] { mutations.query_transforms(invalid_batch); });
  mutations.require_capacity(config.limits.commands);
  world.advance(clock.publish(.016F, {}));
  require(std::abs(required_hit(queries, ray).fraction - 1.0F / 6) < 1.e-5F,
          "Rejected affine batch partially published");
  const auto publication_frame = clock.publish(.016F, {});
  rejects([&] { world.publish_queries(publication_frame); });
  world.advance(publication_frame);
  moved[3][1] = 2;
  mutations.query_transform(instance, moved);
  mutations.query_flags(std::span(&cleared_flags, 1));
  mutations.pose(floor, {.position = {0, -1, 0}});
  const auto before_publication = backing.allocations;
  world.publish_queries(publication_frame);
  require(
      std::abs(required_hit(queries, ray).fraction - 1.0F / 3) < 1.e-5F &&
          queries.body(floor).pose.position[1] == 0 &&
          backing.allocations == before_publication,
      "Post-solve publication must update queries only, without allocation");
  excluded = ray;
  excluded.exclude_flags = 8;
  require(
      required_hit(queries, excluded).receiver == physics::ReceiverId{instance},
      "Post-solve publication must consume query flags with geometry");
  rejects([&] { world.publish_queries(publication_frame); });
  bw::world::WorldClock foreign_clock;
  rejects([&] { world.publish_queries(foreign_clock.publish(.016F, {})); });
  moved[3][1] = 3;
  mutations.query_transform(instance, moved);
  const auto paused_frame = clock.publish(.016F, {.simulation = false});
  world.advance(paused_frame);
  world.publish_queries(paused_frame);
  require(std::abs(required_hit(queries, ray).fraction - 1.0F / 3) < 1.e-5F &&
              queries.body(floor).pose.position[1] == 0,
          "Paused publication consumed pending query or body commands");
  world.advance(clock.publish(.016F, {}));
  require(queries.body(floor).pose.position[1] == -1 &&
              std::abs(required_hit(queries, ray).fraction - 1.0F / 6) < 1.e-5F,
          "Publication discarded next-step solver or query commands");
  rejects([&] { world.publish_queries(publication_frame); });
  rejects([&] { world.remove_shape(mesh); });
  world.remove_query_instance(instance);
  rejects([&] { mutations.query_transform(instance, moved); });
  const auto reused = world.create_query_instance(description);
  require(
      reused.slot == instance.slot && reused.generation != instance.generation,
      "Affine identity reuse retained stale generation");
  excluded = ray;
  excluded.exclude_instance = instance;
  rejects([&] { (void)queries.ray(excluded); });
  auto other = runtime.create_world(config, clock);
  rejects([&] { other.mutations().query_transform(reused, moved); });
  world.remove_query_instance(reused);
  world.remove_body(floor);
  world.remove_shape(mesh);

  // Local shape coordinates can differ from center-of-mass coordinates.
  const auto sphere = world.create_shape(physics::Sphere{1});
  const auto offset = world.create_shape(sphere, {{4, 0, 0}});
  description.shape = offset;
  description.transform = math::affine_part(math::identity_matrix<float, 4>());
  description.transform[0][0] = -2;
  const auto reflected = world.create_query_instance(description);
  const auto sphere_hit =
      queries.ray({.start = {-12, 0, 0}, .end = {-4, 0, 0}});
  require(sphere_hit &&
              sphere_hit->receiver == physics::ReceiverId{reflected} &&
              std::abs(sphere_hit->fraction - .25F) < 1.e-5F &&
              sphere_hit->normal[0] < -.99F,
          "Affine query lost reflection or shape center-of-mass offset");
  world.remove_query_instance(reflected);
  world.remove_shape(offset);
  world.remove_shape(sphere);
}
void bounded_overlaps(bw::physics::Runtime& runtime,
                      const TestBacking& backing) {
  namespace physics = bw::physics;
  bw::world::WorldClock clock;
  physics::Configuration config;
  config.limits = {.bodies = 2,
                   .shapes = 2,
                   .constraints = 1,
                   .body_pairs = 4,
                   .contacts = 4,
                   .commands = 4,
                   .scratch_bytes = 1024 * 1024,
                   .query_instances = 1};
  auto world = runtime.create_world(config, clock);
  constexpr std::size_t kFaces = 40;
  std::array<physics::Vector3, kFaces * 3> vertices{};
  std::array<physics::Triangle, kFaces> triangles{};
  for (std::size_t index = 0; index < kFaces; ++index) {
    const auto height = static_cast<float>(index) * .01F;
    vertices.at(index * 3) = {-10, height, -10};
    vertices.at(index * 3 + 1) = {10, height, -10};
    vertices.at(index * 3 + 2) = {0, height, 10};
    const auto first = static_cast<std::uint32_t>(index * 3);
    triangles.at(index) = {{first, first + 2, first + 1}, 0};
  }
  const auto shape =
      world.create_shape(physics::TriangleMesh{vertices, triangles});
  physics::QueryInstanceDescription description;
  description.shape = shape;
  description.transform =
      bw::math::affine_part(bw::math::identity_matrix<float, 4>());
  description.user_data = 17;
  const auto instance = world.create_query_instance(description);
  const auto queries = world.queries();
  std::array<physics::SphereOverlap, 32> hits{};
  const auto allocations = backing.allocations;
  const physics::SphereQuery query{.center = {0, 0, 0}, .radius = 1};
  const auto count = queries.overlap_sphere(query, hits);
  require(
      count.written == hits.size() && count.truncated,
      "Sphere result budget must retain deepest contacts and report overflow");
  for (std::size_t index = 0; index < hits.size(); ++index) {
    require(hits.at(index).receiver == physics::ReceiverId{instance} &&
                std::abs(hits.at(index).separation -
                         (static_cast<float>(index) * .01F - 1)) < 1.e-4F,
            "Sphere contacts lost ascending penetration order");
  }
  const auto empty = queries.overlap_sphere(query, {});
  require(empty.written == 0 && empty.truncated,
          "Empty output must report existing overlap overflow");
  require(backing.allocations == allocations,
          "Bounded overlap allocated during query");
  world.remove_query_instance(instance);
  auto stale = query;
  stale.exclude_instance = instance;
  rejects([&] { (void)queries.overlap_sphere(stale, hits); });
  world.remove_shape(shape);
  const auto ball = world.create_shape(physics::Sphere{1});
  physics::BodyDescription body;
  body.shape = ball;
  body.pose.position = {1.5F, 0, 0};
  const auto body_id = world.create_body(body);
  const auto rigid = queries.overlap_sphere(query, hits);
  require(rigid.written == 1 && !rigid.truncated &&
              hits.front().receiver == physics::ReceiverId{body_id} &&
              std::abs(hits.front().separation + .5F) < 1.e-4F,
          "Rigid body overlap must retain provider identity and penetration");
  auto excluded = query;
  excluded.exclude = body_id;
  require(queries.overlap_sphere(excluded, hits).written == 0,
          "Rigid body overlap exclusion failed");
  world.remove_body(body_id);
  world.remove_shape(ball);
}
}  // namespace
void physics_world_tests() {
  using bw::physics::BodyDescription;
  using bw::physics::Capsule;
  using bw::physics::Configuration;
  using bw::physics::Motion;
  using bw::physics::Queries;
  using bw::physics::Runtime;
  using bw::physics::Triangle;
  using bw::physics::TriangleMesh;
  using bw::physics::Vector3;
  const auto backing = std::make_shared<TestBacking>();
  auto domain = bw::memory::Domain::create(backing);
  {
    Runtime runtime(domain);
    affine_queries(runtime, *backing);
    bounded_overlaps(runtime, *backing);
    rejects([&] { Runtime duplicate(domain); });
    bw::world::WorldClock clock;
    Configuration configuration;
    configuration.gravity = {0, -9.8F, 0};
    configuration.collision_masks[0] = 3;
    configuration.collision_masks[1] = 3;
    configuration.limits = {.bodies = 16,
                            .shapes = 8,
                            .constraints = 8,
                            .body_pairs = 64,
                            .contacts = 64,
                            .commands = 8,
                            .scratch_bytes = 1024 * 1024};
    std::optional<Queries> expired;
    {
      auto world = runtime.create_world(configuration, clock);
      const auto query = world.queries();
      const auto mutation = world.mutations();
      expired = query;
      constexpr std::array<Vector3, 4> points{
          {{-10, 0, -10}, {10, 0, -10}, {10, 0, 10}, {-10, 0, 10}}};
      constexpr std::array<Triangle, 2> triangles{
          {{{0, 2, 1}, 17}, {{0, 3, 2}, 23}}};
      const auto floor_shape =
          world.create_shape(TriangleMesh{points, triangles});
      BodyDescription floor_description;
      floor_description.shape = floor_shape;
      floor_description.user_data = 100;
      const auto floor = world.create_body(floor_description);
      const auto floor_hit = query.ray({.start = {2, 2, 0}, .end = {2, -2, 0}});
      require(floor_hit &&
                  floor_hit->receiver == bw::physics::ReceiverId{floor} &&
                  floor_hit->shape == floor_shape && floor_hit->surface == 17 &&
                  floor_hit->user_data == 100 &&
                  std::abs(floor_hit->fraction - 0.5F) < 0.001F &&
                  floor_hit->normal[1] > 0.9F,
              "Raw triangle hit preserves identity, material and geometry");
      require(!query.ray({.start = {2, 2, 0}, .end = {2, -2, 0}, .layers = 2}),
              "Query layer filter ignored");
      const auto capsule = world.create_shape(Capsule{0.5F, 0.25F});
      BodyDescription actor;
      actor.shape = capsule;
      actor.pose.position = {0, 2, 0};
      actor.motion = Motion::kinematic;
      actor.layer = 1;
      actor.mass = {1, {1, 1, 1}};
      actor.material.friction = 0.5F;
      actor.user_data = 900;
      actor.surface = 7;
      actor.query_flags = 8;
      const auto body = world.create_body(actor);
      require(
          !query.ray({.start = {-2, 2, 0}, .end = {2, 2, 0}, .exclude = body}),
          "Excluded shooter was hit");
      const auto hit = query.ray({.start = {-2, 2, 0}, .end = {2, 2, 0}});
      require(hit && hit->receiver == bw::physics::ReceiverId{body} &&
                  hit->surface == 7 && hit->user_data == 900,
              "Convex body query lost receiver payload");
      const auto through = query.ray(
          {.start = {0, 4, 0}, .end = {0, -2, 0}, .exclude_flags = 8});
      require(through && through->receiver == bw::physics::ReceiverId{floor},
              "Object filter must find farther eligible terrain");
      rejects([&] { mutation.impulse(body, {1, 0, 0}, {0, 2, 0}); });
      mutation.motion(body, Motion::dynamic);
      mutation.impulse(body, {1, 0, 0}, {0, 2, 0});
      world.advance(clock.publish(1.0F, {.simulation = false}));
      require(query.body(body).motion == Motion::kinematic &&
                  query.body(body).pose.position[1] == 2,
              "Paused world consumed commands or simulation");
      const auto admitted = clock.publish(1.0F / 60, {});
      world.advance(admitted);
      require(query.body(body).motion == Motion::dynamic &&
                  query.body(body).linear_velocity[0] > 0.9F,
              "Queued handoff and impulse failed");
      rejects([&] { world.advance(admitted); });
      bw::world::WorldClock foreign;
      rejects([&] { world.advance(foreign.publish(1.0F / 60, {})); });
      actor.pose.position = {0, 3, 0};
      actor.motion = Motion::dynamic;
      actor.mass.inertia = bw::physics::Inertia::from_shape;
      actor.mass.inertia_diagonal = {};
      const auto second = world.create_body(actor);
      require(
          !query.ray(
              {.start = {0, 5, 0}, .end = {0, 1, 0}, .exclude_user_data = 900}),
          "Copied receiver exclusion did not filter every owned body");
      const auto joint =
          world.create_constraint(bw::physics::DistanceConstraint{
              body, second, {0, 2, 0}, {0, 3, 0}, 1, 1});
      rejects([&] { world.remove_body(body); });
      rejects([&] { world.remove_shape(capsule); });
      for (int frame = 0; frame < 240; ++frame) {
        world.advance(clock.publish(1.0F / 60, {}));
      }
      const auto first_position = query.body(body).pose.position;
      const auto second_position = query.body(second).pose.position;
      const auto distance =
          bw::math::length(bw::math::subtract(first_position, second_position));
      require(std::abs(distance - 1) < 0.1F && first_position[1] > 0,
              "Provider constraint or mesh collision failed");
      world.remove_constraint(joint);
      world.remove_body(body);
      rejects([&] { (void)query.body(body); });
      const auto reused = world.create_body(actor);
      require(reused.slot == body.slot && reused != body,
              "Body reuse revived stale identity");
      auto other = runtime.create_world(configuration, foreign);
      rejects([&] { (void)other.queries().body(reused); });
      world.remove_body(reused);
      world.remove_body(second);
      world.remove_body(floor);
      // Local geometry keeps the bone origin distinct from its center of mass.
      const auto box = world.create_shape(bw::physics::Box{{0.2F, 0.2F, 0.2F}});
      const auto placed = world.create_shape(box, {.position = {0.3F, 0, 0}});
      rejects([&] { world.remove_shape(box); });
      actor.shape = placed;
      actor.motion = Motion::kinematic;
      actor.pose.position = {0, 4, 0};
      const auto parent = world.create_body(actor);
      actor.pose.position = {1, 4, 0};
      const auto child = world.create_body(actor);
      const bw::physics::JointFrame attachment{
          {0.5F, 4, 0}, {0, 0, 1}, {1, 0, 0}};
      const auto hinge = world.create_constraint(bw::physics::HingeConstraint{
          parent, child, attachment, attachment, -0.5F, 0.5F});
      mutation.kinematic_target(parent, {.position = {0, 5, 0}});
      world.advance(clock.publish(1.0F / 60, {.simulation = false}));
      require(query.body(parent).pose.position[1] == 4,
              "Pause consumed the animation target");
      mutation.motion(child, Motion::dynamic);
      world.advance(clock.publish(1.0F / 60, {}));
      require(std::abs(query.body(parent).pose.position[1] - 5) < 0.001F,
              "Kinematic animation target did not reach bone origin");
      rejects([&] { mutation.kinematic_target(child, {}); });
      for (int frame = 0; frame < 60; ++frame) {
        world.advance(clock.publish(1.0F / 60, {}));
      }
      require(bw::math::length(
                  bw::math::subtract(query.body(parent).pose.position,
                                     query.body(child).pose.position)) < 1.2F,
              "Hinge lost attachment after kinematic handoff");
      world.remove_constraint(hinge);
      const auto cone = world.create_constraint(
          bw::physics::SwingTwistConstraint{parent,
                                            child,
                                            attachment,
                                            attachment,
                                            1.0F,
                                            -0.7F,
                                            0.7F,
                                            {2, 40},
                                            {0.5F, 20}});
      for (int frame = 0; frame < 60; ++frame) {
        world.advance(clock.publish(1.0F / 60, {}));
      }
      require(std::isfinite(query.body(child).pose.position[1]),
              "Cone/twist spring constraint produced invalid pose");
      world.remove_constraint(cone);
      world.remove_body(child);
      world.remove_body(parent);
      world.remove_shape(placed);
      world.remove_shape(box);
      world.remove_shape(capsule);
      world.remove_shape(floor_shape);
      require(world.body_count() == 0, "Retirement left live provider bodies");
    }
    rejects([&] { (void)expired->ray({}); });
    expired.reset();
  }
  require(domain->snapshot().live_allocations == 0,
          "Provider allocations survived teardown");
  {
    Runtime replacement(domain);
  }
  require(domain->snapshot().live_allocations == 0,
          "Provider registration cannot be rebuilt cleanly");
}
