// World-owned rigid-body provider. Game policies retain movement, damage and
// animation ownership; this boundary owns geometry, bodies and constraints.
// Runtime admits exactly one live provider registration per process, with an
// explicit retained CPU domain. A second registration throws, never replaces
// allocator callbacks. Worlds retain registration; weak query/mutation handles
// do not retain worlds. Expired handles and stale/cross-world IDs throw.
// All world calls are serial on the constructing thread. Queries return copied
// observations; mutation handles queue bounded value commands. Only World can
// create/retire resources or consume a newer frame from its issuing WorldClock.
// Paused frames consume sequence but neither commands nor simulation time.
// The owner may publish queued affine query poses once after a successful
// solve; this phase preserves queued body commands and never advances the
// solver. I.30 encapsulates this phase authority; MEM.9 keeps compaction
// allocation-free. The issuing clock must outlive its world. Provider
// allocation exhaustion is fatal (upstream allocation is not recoverable);
// resource capacities reject. Commands queued during pause apply once at the
// next admitted frame. Duplicate, foreign, invalid frames fail before mutation.
// No second clock or worker pool. Algorithm: validate descriptor -> prepare
// immutable shape -> admit body; queued commands -> synchronous provider update
// -> copied observation. Bodies retain their shapes; constraints retire before
// attached bodies. Reset/teardown invalidates all IDs. Shape/body/constraint
// capacity and scratch are explicit. Connected-body filtering uses a
// preallocated symmetric bit table (N*(N-1)/2 bits); no allocation during
// collision queries. Affine query instances are distinct from solver bodies.
// Creation prepares inverse transforms and conservative world bounds; updates
// queue fixed copied values, applying only on admitted simulation frames. Ray
// queries filter first, cull cached bounds, cast in immutable local shapes and
// transform normals by inverse transpose. No shape wrappers or query-time
// allocation. Instance scan is O(configured capacity), followed by each
// surviving shape's provider BVH. Both phases Core I.4 (#ri-typed) separates
// receiver identities; performance MEM.9 bounds instance storage at world
// creation. Source 4f3ff0/4fc050 orders mutation and step; 683d40 transfers
// death to bodies. Pinned Jolt and Unreal source comparison:
// first-combat-physics-boundary.md. Design Core I.11 (#ri-raw) keeps ownership
// out of borrowed handles; performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) routes
// storage through one injected engine domain. This is a provider foundation,
// not a generic character controller or a reconstructed PHIT ragdoll schema.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <variant>

#include "bw/math/matrix.hpp"
#include "bw/memory/domain.hpp"
#include "bw/timing/world_clock.hpp"

namespace bw::physics {
using Vector3 = math::Vector3<float>;
inline constexpr std::size_t layer_count = 16;
inline constexpr std::uint16_t all_layers = 0xffff;
struct Pose {
  Vector3 position{};
  std::array<float, 4> rotation{0, 0, 0,
                                1};  // x,y,z,w; finite unit quaternion.
};
template <class Tag>
struct Id {
  std::uint64_t world{};
  std::uint32_t slot{};
  std::uint32_t generation{};
  friend bool operator==(const Id&, const Id&) = default;
};
struct BodyTag;
struct ShapeTag;
struct ConstraintTag;
struct QueryInstanceTag;
using BodyId = Id<BodyTag>;
using ShapeId = Id<ShapeTag>;
using ConstraintId = Id<ConstraintTag>;
using QueryInstanceId = Id<QueryInstanceTag>;
using ReceiverId = std::variant<BodyId, QueryInstanceId>;
struct Sphere {
  float radius{};
};
struct Box {
  Vector3 half_extent{};
};
struct Capsule {
  float half_height{}, radius{};
};
struct Triangle {
  std::array<std::uint32_t, 3> vertices{};
  std::uint32_t surface{};
};
struct TriangleMesh {
  std::span<const Vector3> vertices;
  std::span<const Triangle> triangles;
};
struct ConvexHull {
  std::span<const Vector3> vertices;
};
using Shape = std::variant<Sphere, Box, Capsule, TriangleMesh, ConvexHull>;
enum class Motion : std::uint8_t { stationary, kinematic, dynamic };
// Pair combination uses the higher enum policy. Coefficients are immutable
// body values; providers do not call game code while solving contacts.
enum class MaterialCombine : std::uint8_t {
  average,
  minimum,
  multiply,
  maximum
};
struct Material {
  float friction{}, restitution{};
  MaterialCombine friction_combine{}, restitution_combine{};
};
// Values are explicit imported/selected policy, never anatomical defaults.
// Source4eedb0 requests shape-derived inertia for supplied mass. Core I.4
// (ri-typed) names that choice; MEM.9 keeps the calculation at body creation.
enum class Inertia : std::uint8_t { supplied_diagonal, from_shape };
struct Mass {
  float kilograms{};
  Vector3 inertia_diagonal{};
  Inertia inertia{Inertia::supplied_diagonal};
};
struct BodyDescription {
  ShapeId shape;
  Pose pose;
  Motion motion{Motion::stationary};
  std::uint8_t layer{};
  Material material;
  Mass mass;
  float linear_damping{}, angular_damping{};
  Vector3 linear_velocity{}, angular_velocity{};
  // Explicit finite ceilings prevent an upstream default from clipping source
  // impulses. Original skeletal bodies request unrestricted angular speed.
  float maximum_linear_speed{std::numeric_limits<float>::max()};
  float maximum_angular_speed{std::numeric_limits<float>::max()};
  std::uint64_t user_data{};
  std::uint32_t surface{};
  // Opaque caller classification, tested before nearest-hit selection.
  std::uint32_t query_flags{};
};
// Query geometry preserves full affine animation transforms, including scale
// and shear. It retains an immutable shape but has no solver body or contacts.
// Finite invertible transforms are required. Surface/user values are copied.
struct QueryInstanceDescription {
  ShapeId shape;
  math::Affine3<float> transform{};
  std::uint8_t layer{};
  std::uint64_t user_data{};
  std::uint32_t surface{}, query_flags{};
};
struct QueryTransform {
  QueryInstanceId instance;
  math::Affine3<float> transform{};
};
// Copied query policy is applied in the same world publication phase as poses.
struct QueryFlags {
  QueryInstanceId instance;
  std::uint32_t flags{};
};
struct DistanceConstraint {
  BodyId first, second;
  Vector3 first_world_point{}, second_world_point{};
  float minimum{}, maximum{};
};
// Constraint frames are copied world-space unit axes at construction. Both
// bodies may initially be kinematic; at least one must have movable mass.
// Connected-body collision exclusion is retained until the last such joint
// retires. Springs use physical stiffness/damping, not frequency tuning.
struct JointFrame {
  Vector3 point{}, axis{}, normal{};
};
struct Spring {
  float stiffness{}, damping{};
};
struct HingeConstraint {
  BodyId first, second;
  JointFrame first_frame, second_frame;
  float minimum_angle{}, maximum_angle{};
};
struct SwingTwistConstraint {
  BodyId first, second;
  JointFrame first_frame, second_frame;
  float swing_half_angle{}, twist_minimum{}, twist_maximum{};
  Spring swing_spring, twist_spring;
};
struct BodyState {
  Pose pose;
  Vector3 linear_velocity{}, angular_velocity{};
  Motion motion{};
  bool active{};
};
struct Query {
  Vector3 start{}, end{};
  std::uint16_t layers{all_layers};
  std::optional<BodyId> exclude;
  std::optional<std::uint64_t> exclude_user_data{};
  // Reject a body when any copied query_flags bit intersects this mask.
  std::uint32_t exclude_flags{};
  std::optional<QueryInstanceId> exclude_instance{};
};
struct Hit {
  ReceiverId receiver;
  ShapeId shape;
  std::uint32_t surface{};
  std::uint64_t user_data{};
  Vector3 position{}, normal{};
  float fraction{};
};
// Discrete sphere overlap; no motion sweep is implied. Filters apply before
// geometric testing. Contacts are per face, not deduplicated by receiver.
struct SphereQuery {
  Vector3 center{};
  float radius{};
  std::uint16_t layers{all_layers};
  std::optional<BodyId> exclude{};
  std::optional<QueryInstanceId> exclude_instance{};
  std::optional<std::uint64_t> exclude_user_data{};
  std::uint32_t exclude_flags{};
};
struct SphereOverlap {
  ReceiverId receiver;
  ShapeId shape;
  std::uint64_t user_data{};
  Vector3 position{};
  // Signed surface separation: negative means penetration.
  float separation{};
};
struct OverlapCount {
  std::size_t written{};
  bool truncated{};
};
struct Limits {
  std::uint32_t bodies{}, shapes{}, constraints{};
  std::uint32_t body_pairs{}, contacts{}, commands{};
  std::uint32_t scratch_bytes{};
  std::uint32_t query_instances{};
};
struct Configuration {
  Limits limits;
  Vector3 gravity{};  // Caller selects the authored gravity.
  // Mutual admission is required. No implicit player/enemy/material categories.
  std::array<std::uint16_t, layer_count> collision_masks{};
  // Provider subdivision is explicit; it never caps or discards frame delta.
  std::uint32_t collision_steps{1};
};
namespace detail {
class Runtime;
class World;
}  // namespace detail
class Queries {
 public:
  std::optional<Hit> ray(const Query& query) const;
  // Caller storage retains deepest contacts in ascending separation order.
  // Equal scores preserve provider traversal order; no actor policy/dedup.
  // Supports solver shapes and full-affine mesh instances. An overlapping
  // affine non-mesh instance rejects explicitly, never substitutes a sphere.
  // Fixed triangle batches and output insertion allocate nothing. I.4/MEM.9
  // keep copied observations and bounded caller storage explicit.
  OverlapCount overlap_sphere(const SphereQuery& query,
                              std::span<SphereOverlap> output) const;
  BodyState body(BodyId id) const;

 private:
  friend class World;
  explicit Queries(std::weak_ptr<detail::World> world)
      : world_(std::move(world)) {}
  std::weak_ptr<detail::World> world_;
};
class Mutations {
 public:
  // Serial preflight for an owner's bounded batch; no queue mutation.
  void require_capacity(std::size_t commands) const;
  void pose(BodyId body, const Pose& pose) const;
  // Validate the full batch before publication; failure leaves the queue
  // intact.
  void query_transforms(std::span<const QueryTransform> transforms) const;
  void query_flags(std::span<const QueryFlags> flags) const;
  void query_transform(QueryInstanceId instance,
                       const math::Affine3<float>& transform) const;
  // Move a kinematic bone to its animation target across the admitted delta.
  void kinematic_target(BodyId body, const Pose& pose) const;
  void velocity(BodyId body, Vector3 linear, Vector3 angular) const;
  void motion(BodyId body, Motion motion) const;
  void force(BodyId body, Vector3 force, Vector3 world_point) const;
  void impulse(BodyId body, Vector3 impulse, Vector3 world_point) const;

 private:
  friend class World;
  explicit Mutations(std::weak_ptr<detail::World> world)
      : world_(std::move(world)) {}
  std::weak_ptr<detail::World> world_;
};
// Borrowed creation/retirement authority. Does not expose advancement or clock.
// Expiration means the owning World already retired every provider resource.
class Resources {
 public:
  bool alive() const noexcept { return !world_.expired(); }
  ShapeId create_shape(const Shape& shape) const;
  ShapeId create_shape(ShapeId child, const Pose& local) const;
  BodyId create_body(const BodyDescription& body) const;
  QueryInstanceId create_query_instance(
      const QueryInstanceDescription& instance) const;
  void remove_query_instance(QueryInstanceId id) const;
  ConstraintId create_constraint(const HingeConstraint& constraint) const;
  ConstraintId create_constraint(const SwingTwistConstraint& constraint) const;
  void remove_constraint(ConstraintId id) const;
  void remove_body(BodyId id) const;
  void remove_shape(ShapeId id) const;

 private:
  friend class World;
  explicit Resources(std::weak_ptr<detail::World> world)
      : world_(std::move(world)) {}
  std::weak_ptr<detail::World> world_;
};
class Runtime;
class World {
 public:
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) noexcept = default;
  World& operator=(World&&) noexcept = default;
  ~World();
  ShapeId create_shape(const Shape& shape);
  // Immutable local placement retains the child until this wrapper retires.
  ShapeId create_shape(ShapeId child, const Pose& local);
  BodyId create_body(const BodyDescription& body);
  QueryInstanceId create_query_instance(
      const QueryInstanceDescription& instance);
  void remove_query_instance(QueryInstanceId id);
  ConstraintId create_constraint(const DistanceConstraint& constraint);
  ConstraintId create_constraint(const HingeConstraint& constraint);
  ConstraintId create_constraint(const SwingTwistConstraint& constraint);
  void remove_constraint(ConstraintId id);
  void remove_body(
      BodyId id);  // Rejects while a live constraint references it.
  void remove_shape(ShapeId id);  // Rejects while a live body references it.
  Resources resources();
  Queries queries() const;
  Mutations mutations();
  void advance(const bw::world::WorldFrame& frame);
  // Requires this world's most recent solved frame. Paused current frames do
  // nothing; foreign, unsolved, stale or repeated publications throw. Linear
  // in pending commands; preserves order of retained solver mutations.
  void publish_queries(const bw::world::WorldFrame& frame);
  std::size_t body_count() const;

 private:
  friend class Runtime;
  explicit World(std::shared_ptr<detail::World> world)
      : world_(std::move(world)) {}
  std::shared_ptr<detail::World> world_;
};
class Runtime {
 public:
  explicit Runtime(std::shared_ptr<memory::Domain> domain);
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Runtime(Runtime&&) noexcept = default;
  Runtime& operator=(Runtime&&) noexcept = default;
  ~Runtime();
  World create_world(const Configuration& configuration,
                     const bw::world::WorldClock& clock) const;

 private:
  std::shared_ptr<detail::Runtime> runtime_;
};
}  // namespace bw::physics
