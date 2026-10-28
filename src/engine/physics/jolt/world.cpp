// CPU provider adapter, with recovered queue-before-step ownership. Generic
// geometry/constraint algorithms remain in pinned Jolt; game filters/health and
// original custom character movement remain outside. Core R.1 (#rr-raii) keeps
// provider objects beneath retained registration. Performance MEM.9 bounds
// command/slot storage before updates; MEM.6 routes all provider allocation via
// Runtime's engine domain. Queries and commands are same-thread capabilities.
#include "bw/physics/world.hpp"

#include "engine/physics/jolt/runtime.hpp"

// Both phases I.4 separates affine query identities from rigid solver bodies;
// MEM.9 caches inverse/bounds in fixed instance slots. Jolt local shape casts
// preserve segment fraction under the full affine inverse, with no allocation.
// Runtime includes Jolt.h before these upstream component declarations.
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Geometry/RayAABox.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollideSphereVsTriangles.h>
#include <Jolt/Physics/Collision/CollisionDispatch.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

#include "bw/memory/allocator.hpp"

namespace bw::physics {
namespace {
std::uint64_t next_serial() {
  static std::atomic<std::uint64_t> serial{1};
  return serial.fetch_add(1);
}
void valid(bool condition, const char* message) {
  if (!condition) {
    throw std::invalid_argument(message);
  }
}
void finite(Vector3 value) {
  valid(std::ranges::all_of(value, [](float x) { return std::isfinite(x); }),
        "Nonfinite physics vector");
}
void nonnegative(float value) {
  valid(std::isfinite(value) && value >= 0,
        "Invalid nonnegative physics value");
}
void positive(float value) {
  valid(std::isfinite(value) && value > 0, "Invalid positive physics value");
}
JPH::Vec3 native(Vector3 value) { return {value[0], value[1], value[2]}; }
Vector3 copy(JPH::Vec3 value) {
  return {value.GetX(), value.GetY(), value.GetZ()};
}
JPH::Quat rotation(const Pose& pose) {
  finite(pose.position);
  valid(std::ranges::all_of(pose.rotation,
                            [](float x) { return std::isfinite(x); }),
        "Nonfinite physics rotation");
  JPH::Quat result(pose.rotation[0], pose.rotation[1], pose.rotation[2],
                   pose.rotation[3]);
  valid(result.IsNormalized(), "Physics rotation must be unit length");
  return result;
}
JPH::EMotionType native(Motion motion) {
  switch (motion) {
    case Motion::stationary:
      return JPH::EMotionType::Static;
    case Motion::kinematic:
      return JPH::EMotionType::Kinematic;
    case Motion::dynamic:
      return JPH::EMotionType::Dynamic;
  }
  throw std::invalid_argument("Unknown physics motion type");
}
class BroadLayers final : public JPH::BroadPhaseLayerInterface {
 public:
  JPH::uint GetNumBroadPhaseLayers() const override { return 1; }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override {
    return JPH::BroadPhaseLayer(0);
  }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override {
    return "world";
  }
#endif
};
class BroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override {
    return true;
  }
};
class PairFilter final : public JPH::ObjectLayerPairFilter {
 public:
  explicit PairFilter(std::array<std::uint16_t, layer_count> masks)
      : masks_(masks) {}
  bool ShouldCollide(JPH::ObjectLayer first,
                     JPH::ObjectLayer second) const override {
    return first < masks_.size() && second < masks_.size() &&
           (masks_.at(first) & (1U << second)) != 0 &&
           (masks_.at(second) & (1U << first)) != 0;
  }

 private:
  std::array<std::uint16_t, layer_count> masks_;
};
class QueryLayers final : public JPH::ObjectLayerFilter {
 public:
  explicit QueryLayers(std::uint16_t mask) : mask_(mask) {}
  bool ShouldCollide(JPH::ObjectLayer layer) const override {
    return layer < layer_count && (mask_ & (1U << layer)) != 0;
  }

 private:
  std::uint16_t mask_;
};
// Provider cooking consumes owned copies; all input spans retire after this
// call.
JPH::ShapeRefC prepare(const Shape& description) {
  auto result = std::visit(
      [](const auto& shape) -> JPH::ShapeSettings::ShapeResult {
        using T = std::decay_t<decltype(shape)>;
        if constexpr (std::is_same_v<T, Sphere>) {
          positive(shape.radius);
          JPH::SphereShapeSettings settings(shape.radius);
          settings.SetEmbedded();
          return settings.Create();
        } else if constexpr (std::is_same_v<T, Box>) {
          for (float extent : shape.half_extent) {
            positive(extent);
          }
          JPH::BoxShapeSettings settings(native(shape.half_extent), 0);
          settings.SetEmbedded();
          return settings.Create();
        } else if constexpr (std::is_same_v<T, Capsule>) {
          nonnegative(shape.half_height);
          positive(shape.radius);
          JPH::CapsuleShapeSettings settings(shape.half_height, shape.radius);
          settings.SetEmbedded();
          return settings.Create();
        } else if constexpr (std::is_same_v<T, ConvexHull>) {
          valid(shape.vertices.size() >= 4, "Convex shape needs four vertices");
          JPH::Array<JPH::Vec3> points;
          points.reserve(shape.vertices.size());
          for (const auto& point : shape.vertices) {
            finite(point);
            points.push_back(native(point));
          }
          JPH::ConvexHullShapeSettings settings(points, 0);
          settings.SetEmbedded();
          return settings.Create();
        } else {
          valid(!shape.triangles.empty(), "Empty physics triangle mesh");
          JPH::VertexList vertices;
          vertices.reserve(shape.vertices.size());
          for (const auto& point : shape.vertices) {
            finite(point);
            vertices.emplace_back(point[0], point[1], point[2]);
          }
          JPH::IndexedTriangleList triangles;
          triangles.reserve(shape.triangles.size());
          for (const auto& triangle : shape.triangles) {
            for (auto index : triangle.vertices) {
              valid(index < vertices.size(),
                    "Physics triangle index out of bounds");
            }
            triangles.emplace_back(triangle.vertices[0], triangle.vertices[1],
                                   triangle.vertices[2], 0, triangle.surface);
          }
          JPH::MeshShapeSettings settings(std::move(vertices),
                                          std::move(triangles));
          settings.SetEmbedded();
          settings.mPerTriangleUserData = true;
          return settings.Create();
        }
      },
      description);
  if (result.HasError()) {
    throw std::invalid_argument(result.GetError().c_str());
  }
  return result.Get();
}
}  // namespace
namespace detail {
struct ShapeSlot {
  JPH::ShapeRefC shape;
  std::uint32_t generation{1}, references{};
  std::optional<ShapeId> child;
  bool mesh{};
};
constexpr unsigned kFrictionCombineShift = 32, kRestitutionCombineShift = 34;
constexpr std::uint64_t kCombineMask = 3;
float combine_coefficient(std::array<float, 2> values, std::uint64_t mode) {
  const auto [first, second] = values;
  constexpr float kAverageWeight = .5F;
  switch (static_cast<MaterialCombine>(mode)) {
    case MaterialCombine::average:
      return kAverageWeight * (first + second);
    case MaterialCombine::minimum:
      return std::min(first, second);
    case MaterialCombine::multiply:
      return first * second;
    case MaterialCombine::maximum:
      return std::max(first, second);
  }
  std::terminate();
}
std::uint64_t combination(const JPH::Body& first, const JPH::Body& second,
                          unsigned shift) {
  return std::max((first.GetUserData() >> shift) & kCombineMask,
                  (second.GetUserData() >> shift) & kCombineMask);
}
float combine_friction(const JPH::Body& first, const JPH::SubShapeID&,
                       const JPH::Body& second, const JPH::SubShapeID&) {
  return combine_coefficient({first.GetFriction(), second.GetFriction()},
                             combination(first, second, kFrictionCombineShift));
}
float combine_restitution(const JPH::Body& first, const JPH::SubShapeID&,
                          const JPH::Body& second, const JPH::SubShapeID&) {
  return combine_coefficient(
      {first.GetRestitution(), second.GetRestitution()},
      combination(first, second, kRestitutionCombineShift));
}
struct AffineGeometry {
  math::Affine3<float> transform{}, inverse{};
  JPH::AABox bounds{JPH::Vec3::sZero(), JPH::Vec3::sZero()};
};
// Cache inverse and conservative bounds when publishing an instance transform.
// Jolt shape coordinates are relative to their immutable center of mass.
AffineGeometry prepare_affine(const JPH::Shape& shape,
                              const math::Affine3<float>& transform) {
  valid(math::all_finite(transform), "Nonfinite query instance transform");
  const auto inverse = math::inverse_affine(transform);
  if (!inverse || !math::all_finite(*inverse)) {
    throw std::invalid_argument("Singular query instance transform");
  }
  const auto center =
      math::transform_point<float>(copy(shape.GetCenterOfMass()), transform);
  const auto column = [](const Vector3& row) {
    return JPH::Vec4(row[0], row[1], row[2], 0);
  };
  const JPH::Mat44 matrix(column(transform[0]), column(transform[1]),
                          column(transform[2]), native(center));
  const auto bounds = shape.GetLocalBounds().Transformed(matrix);
  finite(copy(bounds.mMin));
  finite(copy(bounds.mMax));
  return {transform, *inverse, bounds};
}
// Query-local bounded collection follows the source ascending separation
// ordering. The pinned provider computes contacts; no actor dedup occurs here.
class SphereResults final : public JPH::CollideShapeCollector {
 public:
  explicit SphereResults(std::span<SphereOverlap> output) : output_(output) {}
  void receiver(ReceiverId id, ShapeId shape, std::uint64_t user_data) {
    current_ = {id, shape, user_data, {}, 0};
  }
  void AddHit(const JPH::CollideShapeResult& result) override {
    auto hit = current_;
    hit.position = copy(result.mContactPointOn2);
    hit.separation = -result.mPenetrationDepth;
    std::size_t index = 0;
    while (index < count_.written &&
           output_[index].separation <= hit.separation) {
      ++index;
    }
    if (count_.written == output_.size()) count_.truncated = true;
    if (index == output_.size()) return;
    if (count_.written < output_.size()) ++count_.written;
    for (auto move = count_.written - 1; move > index; --move) {
      output_[move] = output_[move - 1];
    }
    output_[index] = hit;
  }
  OverlapCount count() const noexcept { return count_; }

 private:
  std::span<SphereOverlap> output_;
  SphereOverlap current_{};
  OverlapCount count_{};
};
JPH::Mat44 affine_matrix(const math::Affine3<float>& transform) {
  const auto column = [](const Vector3& row) {
    return JPH::Vec4(row[0], row[1], row[2], 0);
  };
  return {column(transform[0]), column(transform[1]), column(transform[2]),
          native(transform[3])};
}
struct QueryInstanceSlot {
  AffineGeometry geometry;
  ShapeId shape;
  std::uint64_t user_data{};
  std::uint32_t generation{1}, surface{}, query_flags{};
  std::uint8_t layer{};
  bool live{};
};
struct BodySlot {
  JPH::BodyID body;
  std::uint32_t generation{1};
  bool live{}, movable{};
  ShapeId shape;
  Motion motion{}, pending_motion{};
  std::uint64_t user_data{};
  std::uint32_t surface{}, query_flags{};
};
// Game receiver exclusion is a copied value; the filter borrows only the
// serial world's immutable slot view during this query, never a game callback.
class QueryBodies final : public JPH::BodyFilter {
 public:
  QueryBodies(std::span<const BodySlot> slots, JPH::BodyID excluded,
              std::optional<std::uint64_t> receiver, std::uint32_t flags)
      : slots_(slots),
        receiver_(receiver),
        excluded_(excluded),
        flags_(flags) {}
  bool ShouldCollide(const JPH::BodyID& body) const override {
    return body != excluded_;
  }
  bool ShouldCollideLocked(const JPH::Body& body) const override {
    const auto index = static_cast<std::uint32_t>(body.GetUserData());
    return index < slots_.size() &&
           (!receiver_ || slots_[index].user_data != *receiver_) &&
           (slots_[index].query_flags & flags_) == 0;
  }

 private:
  std::span<const BodySlot> slots_;
  std::optional<std::uint64_t> receiver_;
  JPH::BodyID excluded_;
  std::uint32_t flags_;
};
struct ConstraintSlot {
  JPH::Ref<JPH::TwoBodyConstraint> constraint;
  std::uint32_t generation{1};
  BodyId first, second;
  bool exclude_collision{};
};
enum class CommandKind : std::uint8_t {
  pose,
  kinematic_target,
  velocity,
  motion,
  force,
  impulse,
  query_transform,
  query_flags
};
struct Command {
  CommandKind kind{};
  BodyId body;
  Pose pose;
  Vector3 first{}, second{};
  Motion motion{};
  QueryInstanceId instance;
  AffineGeometry affine;
  std::uint32_t flags{};
};
class World {
 public:
  World(std::shared_ptr<Runtime> runtime, Configuration configuration,
        const bw::world::WorldClock& clock)
      : runtime_(std::move(runtime)),
        clock_(clock),
        thread_(std::this_thread::get_id()),
        serial_(next_serial()),
        pair_filter_(configuration.collision_masks),
        scratch_(configuration.limits.scratch_bytes),
        group_filter_(configuration.limits.bodies),
        shapes_(memory::Allocator<ShapeSlot>(runtime_->domain())),
        bodies_(memory::Allocator<BodySlot>(runtime_->domain())),
        instances_(memory::Allocator<QueryInstanceSlot>(runtime_->domain())),
        constraints_(memory::Allocator<ConstraintSlot>(runtime_->domain())),
        commands_(memory::Allocator<Command>(runtime_->domain())),
        configuration_(configuration),
        jobs_(JPH::cMaxPhysicsJobs) {
    group_filter_.SetEmbedded();
    valid(serial_ != 0, "Physics world identity exhausted");
    const auto& limits = configuration_.limits;
    shapes_.resize(limits.shapes);
    bodies_.resize(limits.bodies);
    instances_.resize(limits.query_instances);
    constraints_.resize(limits.constraints);
    commands_.reserve(limits.commands);
    system_.Init(limits.bodies, 0, limits.body_pairs, limits.contacts,
                 broad_layers_, broad_filter_, pair_filter_);
    system_.SetGravity(native(configuration.gravity));
    system_.SetCombineFriction(combine_friction);
    system_.SetCombineRestitution(combine_restitution);
  }
  ~World() {
    if (thread_ != std::this_thread::get_id()) {
      std::terminate();
    }
    for (auto& slot : constraints_) {
      if (slot.constraint != nullptr) {
        system_.RemoveConstraint(slot.constraint);
        slot.constraint = nullptr;
      }
    }
    auto& interface = system_.GetBodyInterface();
    for (auto& slot : bodies_) {
      if (slot.live) {
        interface.RemoveBody(slot.body);
        interface.DestroyBody(slot.body);
        slot.live = false;
      }
    }
    for (auto& slot : shapes_) {
      slot.shape = nullptr;
    }
  }
  World(const World&) = delete;
  World& operator=(const World&) = delete;
  World(World&&) = delete;
  World& operator=(World&&) = delete;
  void thread() const {
    if (thread_ != std::this_thread::get_id()) {
      throw std::logic_error("Physics world used on another thread");
    }
  }
  template <class Tag, class Slot>
  Slot& checked(Id<Tag> id, memory::Vector<Slot>& slots) {
    thread();
    valid(id.world == serial_ && id.slot < slots.size() &&
              id.generation == slots[id.slot].generation,
          "Stale or foreign physics identity");
    return slots[id.slot];
  }
  ShapeSlot& shape(ShapeId id) {
    auto& slot = checked(id, shapes_);
    valid(slot.shape != nullptr, "Retired physics shape");
    return slot;
  }
  BodySlot& body(BodyId id) {
    auto& slot = checked(id, bodies_);
    valid(slot.live, "Retired physics body");
    return slot;
  }
  QueryInstanceSlot& instance(QueryInstanceId id) {
    auto& slot = checked(id, instances_);
    valid(slot.live, "Retired query instance");
    return slot;
  }
  ConstraintSlot& constraint(ConstraintId id) {
    auto& slot = checked(id, constraints_);
    valid(slot.constraint != nullptr, "Retired physics constraint");
    return slot;
  }
  template <class Slot, class Available>
  static std::uint32_t vacant(const memory::Vector<Slot>& slots,
                              Available available) {
    for (std::size_t index = 0; index < slots.size(); ++index) {
      if (available(slots[index]) && slots[index].generation != UINT32_MAX) {
        return static_cast<std::uint32_t>(index);
      }
    }
    throw std::length_error("Physics resource capacity exhausted");
  }
  ShapeId create_shape(const Shape& description) {
    thread();
    auto index = vacant(
        shapes_, [](const ShapeSlot& slot) { return slot.shape == nullptr; });
    auto prepared = prepare(description);
    auto& slot = shapes_[index];
    slot.shape = prepared;
    slot.mesh = std::holds_alternative<TriangleMesh>(description);
    return {serial_, index, slot.generation};
  }
  ShapeId create_shape(ShapeId child, const Pose& local) {
    auto& retained = shape(child);
    const auto orientation = rotation(local);
    auto index = vacant(
        shapes_, [](const ShapeSlot& slot) { return slot.shape == nullptr; });
    JPH::RotatedTranslatedShapeSettings settings(native(local.position),
                                                 orientation, retained.shape);
    settings.SetEmbedded();
    const auto result = settings.Create();
    if (result.HasError()) {
      throw std::invalid_argument(result.GetError().c_str());
    }
    auto& slot = shapes_[index];
    slot.shape = result.Get();
    slot.child = child;
    slot.mesh = retained.mesh;
    ++retained.references;
    return {serial_, index, slot.generation};
  }
  QueryInstanceId create_query_instance(const QueryInstanceDescription& value) {
    auto& retained = shape(value.shape);
    valid(value.layer < layer_count, "Invalid query instance layer");
    const auto prepared = prepare_affine(*retained.shape, value.transform);
    const auto index =
        vacant(instances_, [](const auto& slot) { return !slot.live; });
    auto& slot = instances_[index];
    slot.geometry = prepared;
    slot.shape = value.shape;
    slot.layer = value.layer;
    slot.user_data = value.user_data;
    slot.surface = value.surface;
    slot.query_flags = value.query_flags;
    slot.live = true;
    ++retained.references;
    return {serial_, index, slot.generation};
  }
  void remove_query_instance(QueryInstanceId id) {
    auto& slot = instance(id);
    --shape(slot.shape).references;
    slot.live = false;
    ++slot.generation;
    std::erase_if(commands_, [id](const Command& command) {
      return (command.kind == CommandKind::query_transform ||
              command.kind == CommandKind::query_flags) &&
             command.instance == id;
    });
  }
  void query_transforms(std::span<const QueryTransform> transforms) {
    require_capacity(transforms.size());
    const auto old_size = commands_.size();
    try {
      for (const auto& value : transforms) {
        const auto& slot = instance(value.instance);
        const auto prepared =
            prepare_affine(*shape(slot.shape).shape, value.transform);
        commands_.push_back({.kind = CommandKind::query_transform,
                             .instance = value.instance,
                             .affine = prepared});
      }
    } catch (...) {
      commands_.resize(old_size);
      throw;
    }
  }
  void query_flags(std::span<const QueryFlags> values) {
    require_capacity(values.size());
    for (const auto& value : values) (void)instance(value.instance);
    for (const auto& value : values) {
      commands_.push_back({.kind = CommandKind::query_flags,
                           .instance = value.instance,
                           .flags = value.flags});
    }
  }
  BodyId create_body(const BodyDescription& description) {
    auto& retained = shape(description.shape);
    auto index =
        vacant(bodies_, [](const BodySlot& slot) { return !slot.live; });
    valid(description.layer < layer_count,
          "Physics layer exceeds configured matrix");
    const auto type = native(description.motion);
    const auto orientation = rotation(description.pose);
    finite(description.linear_velocity);
    finite(description.angular_velocity);
    nonnegative(description.material.friction);
    nonnegative(description.material.restitution);
    valid(description.material.restitution <= 1,
          "Physics restitution exceeds one");
    nonnegative(description.linear_damping);
    nonnegative(description.angular_damping);
    positive(description.maximum_linear_speed);
    positive(description.maximum_angular_speed);
    const bool movable = description.motion != Motion::stationary;
    valid(!retained.mesh || !movable, "Triangle mesh must remain stationary");
    if (movable) {
      positive(description.mass.kilograms);
      valid(description.mass.inertia == Inertia::supplied_diagonal ||
                description.mass.inertia == Inertia::from_shape,
            "Unknown inertia policy");
      if (description.mass.inertia == Inertia::supplied_diagonal) {
        for (float value : description.mass.inertia_diagonal) {
          positive(value);
        }
      }
    }
    JPH::BodyCreationSettings settings(retained.shape,
                                       native(description.pose.position),
                                       orientation, type, description.layer);
    settings.mFriction = description.material.friction;
    settings.mRestitution = description.material.restitution;
    settings.mLinearDamping = description.linear_damping;
    settings.mAngularDamping = description.angular_damping;
    settings.mMaxLinearVelocity = description.maximum_linear_speed;
    settings.mMaxAngularVelocity = description.maximum_angular_speed;
    settings.mLinearVelocity = native(description.linear_velocity);
    settings.mAngularVelocity = native(description.angular_velocity);
    valid(static_cast<unsigned>(description.material.friction_combine) <=
                  kCombineMask &&
              static_cast<unsigned>(description.material.restitution_combine) <=
                  kCombineMask,
          "Invalid material combination mode");
    settings.mUserData =
        index |
        (static_cast<std::uint64_t>(description.material.friction_combine)
         << kFrictionCombineShift) |
        (static_cast<std::uint64_t>(description.material.restitution_combine)
         << kRestitutionCombineShift);
    settings.mCollisionGroup = JPH::CollisionGroup(&group_filter_, 0, index);
    if (movable) {
      // Both-phase I.4 and MEM.9: source requests a named creation-time policy,
      // not a guessed unit inertia or recalculation during simulation.
      settings.mOverrideMassProperties =
          description.mass.inertia == Inertia::from_shape
              ? JPH::EOverrideMassProperties::CalculateInertia
              : JPH::EOverrideMassProperties::MassAndInertiaProvided;
      settings.mMassPropertiesOverride.mMass = description.mass.kilograms;
      settings.mMassPropertiesOverride.mInertia =
          JPH::Mat44::sScale(native(description.mass.inertia_diagonal));
    }
    auto& interface = system_.GetBodyInterface();
    const auto created = interface.CreateAndAddBody(
        settings,
        movable ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    if (created.IsInvalid()) {
      throw std::length_error("Provider body capacity exhausted");
    }
    auto& slot = bodies_[index];
    slot.body = created;
    slot.live = true;
    slot.movable = movable;
    slot.shape = description.shape;
    slot.motion = description.motion;
    slot.pending_motion = description.motion;
    slot.user_data = description.user_data;
    slot.query_flags = description.query_flags;
    slot.surface = description.surface;
    ++retained.references;
    return {serial_, index, slot.generation};
  }
  ConstraintId create_constraint(const DistanceConstraint& description) {
    auto& first = body(description.first);
    auto& second = body(description.second);
    valid(description.first != description.second,
          "Constraint requires distinct bodies");
    valid(first.motion == Motion::dynamic || second.motion == Motion::dynamic,
          "Constraint requires a dynamic body");
    finite(description.first_world_point);
    finite(description.second_world_point);
    nonnegative(description.minimum);
    nonnegative(description.maximum);
    valid(description.minimum <= description.maximum,
          "Reversed constraint limits");
    JPH::DistanceConstraintSettings settings;
    settings.SetEmbedded();
    settings.mPoint1 = native(description.first_world_point);
    settings.mPoint2 = native(description.second_world_point);
    settings.mMinDistance = description.minimum;
    settings.mMaxDistance = description.maximum;
    return publish_constraint(description.first, description.second, settings,
                              false);
  }
  // Validate world frames before handing the coupled solver to the provider.
  static void frame(const JointFrame& value) {
    finite(value.point);
    finite(value.axis);
    finite(value.normal);
    constexpr float kFrameTolerance = 1.0e-4F;
    valid(std::abs(math::dot(value.axis, value.axis) - 1) < kFrameTolerance &&
              std::abs(math::dot(value.normal, value.normal) - 1) <
                  kFrameTolerance &&
              std::abs(math::dot(value.axis, value.normal)) < kFrameTolerance,
          "Constraint frame must be orthonormal");
  }
  static void angles(float minimum, float maximum) {
    valid(std::isfinite(minimum) && std::isfinite(maximum) &&
              minimum >= -std::numbers::pi_v<float> && minimum <= 0 &&
              maximum >= 0 && maximum <= std::numbers::pi_v<float>,
          "Invalid angular constraint limits");
  }
  ConstraintId create_constraint(const HingeConstraint& description) {
    frame(description.first_frame);
    frame(description.second_frame);
    angles(description.minimum_angle, description.maximum_angle);
    JPH::HingeConstraintSettings settings;
    settings.SetEmbedded();
    settings.mPoint1 = native(description.first_frame.point);
    settings.mPoint2 = native(description.second_frame.point);
    settings.mHingeAxis1 = native(description.first_frame.axis);
    settings.mHingeAxis2 = native(description.second_frame.axis);
    settings.mNormalAxis1 = native(description.first_frame.normal);
    settings.mNormalAxis2 = native(description.second_frame.normal);
    settings.mLimitsMin = description.minimum_angle;
    settings.mLimitsMax = description.maximum_angle;
    return publish_constraint(description.first, description.second, settings,
                              true);
  }
  ConstraintId create_constraint(const SwingTwistConstraint& description) {
    frame(description.first_frame);
    frame(description.second_frame);
    angles(description.twist_minimum, description.twist_maximum);
    nonnegative(description.swing_half_angle);
    valid(description.swing_half_angle <= std::numbers::pi_v<float>,
          "Invalid swing cone angle");
    nonnegative(description.swing_spring.stiffness);
    nonnegative(description.swing_spring.damping);
    nonnegative(description.twist_spring.stiffness);
    nonnegative(description.twist_spring.damping);
    JPH::SwingTwistConstraintSettings settings;
    settings.SetEmbedded();
    settings.mPosition1 = native(description.first_frame.point);
    settings.mPosition2 = native(description.second_frame.point);
    settings.mTwistAxis1 = native(description.first_frame.axis);
    settings.mTwistAxis2 = native(description.second_frame.axis);
    settings.mPlaneAxis1 = native(description.first_frame.normal);
    settings.mPlaneAxis2 = native(description.second_frame.normal);
    settings.mNormalHalfConeAngle = description.swing_half_angle;
    settings.mPlaneHalfConeAngle = description.swing_half_angle;
    settings.mTwistMinAngle = description.twist_minimum;
    settings.mTwistMaxAngle = description.twist_maximum;
    settings.mSwingMotorSettings.mSpringSettings = {
        JPH::ESpringMode::StiffnessAndDamping,
        description.swing_spring.stiffness, description.swing_spring.damping};
    settings.mTwistMotorSettings.mSpringSettings = {
        JPH::ESpringMode::StiffnessAndDamping,
        description.twist_spring.stiffness, description.twist_spring.damping};
    const auto id = publish_constraint(description.first, description.second,
                                       settings, true);
    auto* created = dynamic_cast<JPH::SwingTwistConstraint*>(
        constraint(id).constraint.GetPtr());
    if (description.swing_spring.stiffness > 0 ||
        description.swing_spring.damping > 0) {
      created->SetSwingMotorState(JPH::EMotorState::Position);
    }
    if (description.twist_spring.stiffness > 0 ||
        description.twist_spring.damping > 0) {
      created->SetTwistMotorState(JPH::EMotorState::Position);
    }
    created->SetTargetOrientationCS(JPH::Quat::sIdentity());
    return id;
  }
  ConstraintId publish_constraint(
      BodyId first_id, BodyId second_id,
      const JPH::TwoBodyConstraintSettings& settings, bool exclude) {
    auto& first = body(first_id);
    auto& second = body(second_id);
    valid(first_id != second_id && (first.movable || second.movable),
          "Constraint requires distinct bodies and movable mass");
    auto index = vacant(constraints_, [](const ConstraintSlot& slot) {
      return slot.constraint == nullptr;
    });
    auto& slot = constraints_[index];
    slot.constraint = system_.GetBodyInterface().CreateConstraint(
        &settings, first.body, second.body);
    system_.AddConstraint(slot.constraint);
    slot.first = first_id;
    slot.second = second_id;
    slot.exclude_collision = exclude;
    if (exclude) {
      group_filter_.DisableCollision(first_id.slot, second_id.slot);
      system_.GetBodyInterface().InvalidateContactCache(first.body);
    }
    return {serial_, index, slot.generation};
  }
  void remove_constraint(ConstraintId id) {
    auto& slot = constraint(id);
    system_.RemoveConstraint(slot.constraint);
    slot.constraint = nullptr;
    if (slot.exclude_collision) {
      const bool retained =
          std::ranges::any_of(constraints_, [&](const auto& other) {
            return other.constraint != nullptr && other.exclude_collision &&
                   ((other.first == slot.first &&
                     other.second == slot.second) ||
                    (other.first == slot.second && other.second == slot.first));
          });
      if (!retained) {
        group_filter_.EnableCollision(slot.first.slot, slot.second.slot);
        system_.GetBodyInterface().InvalidateContactCache(
            body(slot.first).body);
      }
    }
    ++slot.generation;
  }
  void remove_body(BodyId id) {
    auto& slot = body(id);
    for (const auto& joint : constraints_) {
      valid(joint.constraint == nullptr ||
                (joint.first != id && joint.second != id),
            "Remove attached constraints before body");
    }
    auto& interface = system_.GetBodyInterface();
    interface.RemoveBody(slot.body);
    interface.DestroyBody(slot.body);
    --shape(slot.shape).references;
    slot.live = false;
    ++slot.generation;
    std::erase_if(commands_,
                  [id](const Command& command) { return command.body == id; });
  }
  void remove_shape(ShapeId id) {
    auto& slot = shape(id);
    valid(slot.references == 0, "Remove bodies before physics shape");
    slot.shape = nullptr;
    if (slot.child) {
      --shape(*slot.child).references;
      slot.child.reset();
    }
    ++slot.generation;
  }
  void require_capacity(std::size_t count) const {
    thread();
    valid(count <= configuration_.limits.commands - commands_.size(),
          "Physics command batch exceeds remaining capacity");
  }
  void queue(Command command) {
    auto& slot = body(command.body);
    valid(commands_.size() < configuration_.limits.commands,
          "Physics command capacity exhausted");
    if (command.kind == CommandKind::motion) {
      native(command.motion);
      valid(slot.movable || command.motion == Motion::stationary,
            "Stationary body has no dynamic mass properties");
    }
    if (command.kind == CommandKind::force ||
        command.kind == CommandKind::impulse) {
      valid(slot.pending_motion == Motion::dynamic,
            "Force requires dynamic body ownership");
    }
    if (command.kind == CommandKind::kinematic_target) {
      valid(slot.pending_motion == Motion::kinematic,
            "Kinematic target requires animation body ownership");
    }
    if (command.kind == CommandKind::velocity) {
      valid(slot.pending_motion != Motion::stationary,
            "Velocity requires movable body");
    }
    commands_.push_back(command);
    if (command.kind == CommandKind::motion) {
      slot.pending_motion = command.motion;
    }
  }
  void advance(const bw::world::WorldFrame& frame) {
    thread();
    valid(frame.belongs_to(clock_) && frame.sequence() > last_sequence_,
          "Foreign or repeated physics frame");
    valid(std::isfinite(frame.delta_seconds()) && frame.delta_seconds() >= 0,
          "Invalid physics frame delta");
    last_sequence_ = frame.sequence();
    if (!frame.admission().simulation || frame.delta_seconds() == 0) {
      return;
    }
    auto& interface = system_.GetBodyInterface();
    for (const auto& command : commands_) {
      if (command.kind == CommandKind::query_transform) {
        instance(command.instance).geometry = command.affine;
        continue;
      }
      if (command.kind == CommandKind::query_flags) {
        instance(command.instance).query_flags = command.flags;
        continue;
      }
      auto& slot = body(command.body);
      switch (command.kind) {
        case CommandKind::query_transform:
        case CommandKind::query_flags:
          break;  // Applied above without rigid-body access.
        case CommandKind::pose:
          interface.SetPositionAndRotation(
              slot.body, native(command.pose.position), rotation(command.pose),
              JPH::EActivation::Activate);
          break;
        case CommandKind::kinematic_target:
          interface.MoveKinematic(slot.body, native(command.pose.position),
                                  rotation(command.pose),
                                  frame.delta_seconds());
          break;
        case CommandKind::velocity:
          interface.SetLinearAndAngularVelocity(
              slot.body, native(command.first), native(command.second));
          break;
        case CommandKind::motion:
          interface.SetMotionType(slot.body, native(command.motion),
                                  JPH::EActivation::Activate);
          slot.motion = command.motion;
          break;
        case CommandKind::force:
          interface.AddForce(slot.body, native(command.first),
                             native(command.second));
          break;
        case CommandKind::impulse:
          interface.AddImpulse(slot.body, native(command.first),
                               native(command.second));
          break;
      }
    }
    commands_.clear();
    if (frame.delta_seconds() > 0) {
      const auto error = system_.Update(
          frame.delta_seconds(),
          static_cast<int>(configuration_.collision_steps), &scratch_, &jobs_);
      if (error != JPH::EPhysicsUpdateError::None) {
        throw std::runtime_error("Physics update exceeded configured capacity");
      }
    }
    solved_sequence_ = frame.sequence();
  }
  // Post-solve publication refreshes solved skeletal query geometry without
  // draining next-step impulses/targets or entering the solver a second time.
  // Core I.30 contains phase authority; MEM.9 keeps stable compaction in place.
  void publish_queries(const bw::world::WorldFrame& frame) {
    thread();
    valid(frame.belongs_to(clock_) && frame.sequence() == last_sequence_,
          "Foreign or stale query publication frame");
    if (!frame.admission().simulation || frame.delta_seconds() == 0) return;
    valid(frame.sequence() == solved_sequence_ &&
              frame.sequence() > published_sequence_,
          "Unsolved or repeated query publication frame");
    std::erase_if(commands_, [this](const Command& command) {
      if (command.kind == CommandKind::query_flags) {
        instance(command.instance).query_flags = command.flags;
        return true;
      }
      if (command.kind != CommandKind::query_transform) return false;
      instance(command.instance).geometry = command.affine;
      return true;
    });
    published_sequence_ = frame.sequence();
  }
  BodyState observe(BodyId id) {
    auto& slot = body(id);
    auto& interface = system_.GetBodyInterface();
    auto position = interface.GetPosition(slot.body);
    auto orientation = interface.GetRotation(slot.body);
    return {{copy(position),
             {orientation.GetX(), orientation.GetY(), orientation.GetZ(),
              orientation.GetW()}},
            copy(interface.GetLinearVelocity(slot.body)),
            copy(interface.GetAngularVelocity(slot.body)),
            slot.motion,
            interface.IsActive(slot.body)};
  }
  std::optional<Hit> ray_body(const Query& query, Vector3 delta) {
    JPH::BodyID excluded;
    if (query.exclude) excluded = body(*query.exclude).body;
    JPH::RayCastResult result;
    if (!system_.GetNarrowPhaseQuery().CastRay(
            {native(query.start), native(delta)}, result, {},
            QueryLayers(query.layers),
            QueryBodies(bodies_, excluded, query.exclude_user_data,
                        query.exclude_flags))) {
      return std::nullopt;
    }
    JPH::BodyLockRead lock(system_.GetBodyLockInterface(), result.mBodyID);
    if (!lock.Succeeded()) {
      throw std::logic_error("Physics query lost a serial body");
    }
    const auto& receiver = lock.GetBody();
    auto index = static_cast<std::uint32_t>(receiver.GetUserData());
    auto& slot = bodies_.at(index);
    BodyId id{serial_, index, slot.generation};
    const auto point =
        math::add(query.start, math::scale(delta, result.mFraction));
    auto& retained = shape(slot.shape);
    auto surface = slot.surface;
    surface = surface_id(retained, result.mSubShapeID2, surface);
    return Hit{id,
               slot.shape,
               surface,
               slot.user_data,
               point,
               copy(receiver.GetWorldSpaceSurfaceNormal(result.mSubShapeID2,
                                                        native(point))),
               result.mFraction};
  }
  static std::uint32_t surface_id(const ShapeSlot& retained,
                                  JPH::SubShapeID subshape,
                                  std::uint32_t fallback) {
    if (!retained.mesh) return fallback;
    JPH::SubShapeID remainder;
    const auto* leaf = retained.shape->GetLeafShape(subshape, remainder);
    return dynamic_cast<const JPH::MeshShape*>(leaf)->GetTriangleUserData(
        remainder);
  }
  // Bodies and affine instances compete in one fraction domain. Reject copied
  // filters before nearest-hit selection so excluded geometry cannot occlude.
  std::optional<Hit> ray(const Query& query) {
    thread();
    finite(query.start);
    finite(query.end);
    if (query.exclude) (void)body(*query.exclude);
    if (query.exclude_instance) (void)instance(*query.exclude_instance);
    const auto delta = math::subtract(query.end, query.start);
    finite(delta);
    if (math::dot(delta, delta) == 0) return std::nullopt;
    auto nearest = ray_body(query, delta);
    const JPH::RayInvDirection inverse_direction(native(delta));
    for (std::size_t index = 0; index < instances_.size(); ++index) {
      const auto& slot = instances_[index];
      const QueryInstanceId id{serial_, static_cast<std::uint32_t>(index),
                               slot.generation};
      if (!slot.live || (query.layers & (1U << slot.layer)) == 0 ||
          query.exclude_instance == id ||
          (query.exclude_user_data &&
           *query.exclude_user_data == slot.user_data) ||
          (slot.query_flags & query.exclude_flags) != 0) {
        continue;
      }
      JPH::RayCastResult result;
      if (nearest) result.mFraction = nearest->fraction;
      if (JPH::RayAABox(native(query.start), inverse_direction,
                        slot.geometry.bounds.mMin,
                        slot.geometry.bounds.mMax) > result.mFraction) {
        continue;
      }
      const auto& retained = shape(slot.shape);
      const auto origin = native(math::transform_point<float>(
                              query.start, slot.geometry.inverse)) -
                          retained.shape->GetCenterOfMass();
      const auto direction = native(
          math::transform_direction<float>(delta, slot.geometry.inverse));
      const JPH::RayCast local_ray(origin, direction);
      if (!retained.shape->CastRay(local_ray, JPH::SubShapeIDCreator{},
                                   result)) {
        continue;
      }
      const auto local_normal = copy(retained.shape->GetSurfaceNormal(
          result.mSubShapeID2, local_ray.GetPointOnRay(result.mFraction)));
      // A row-stored inverse multiplied by a column normal is inverse transpose
      // in the public row-vector convention; translation never participates.
      math::Matrix<float, 3, 3> inverse_basis{slot.geometry.inverse[0],
                                              slot.geometry.inverse[1],
                                              slot.geometry.inverse[2]};
      const auto normal = math::multiply(inverse_basis, local_normal);
      const auto length = math::length(normal);
      positive(length);
      nearest =
          Hit{id,
              slot.shape,
              surface_id(retained, result.mSubShapeID2, slot.surface),
              slot.user_data,
              math::add(query.start, math::scale(delta, result.mFraction)),
              math::divide(normal, length),
              result.mFraction};
    }
    return nearest;
  }
  OverlapCount overlap_sphere(const SphereQuery& query,
                              std::span<SphereOverlap> output) {
    thread();
    finite(query.center);
    positive(query.radius);
    if (query.exclude) (void)body(*query.exclude);
    if (query.exclude_instance) (void)instance(*query.exclude_instance);
    const auto center = native(query.center);
    const auto extent = JPH::Vec3::sReplicate(query.radius);
    const JPH::AABox bounds(center - extent, center + extent);
    finite(copy(bounds.mMin));
    finite(copy(bounds.mMax));
    JPH::SphereShape sphere(query.radius);
    JPH::CollideShapeSettings settings;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    settings.mActiveEdgeMode = JPH::EActiveEdgeMode::CollideWithAll;
    const auto sphere_transform = JPH::Mat44::sTranslation(center);
    SphereResults results(output);
    // Stable resource slot traversal; provider narrowphase emits all contacts.
    // Layer/identity/flag exclusion precedes any contact budget consumption.
    for (std::size_t index = 0; index < bodies_.size(); ++index) {
      const auto& slot = bodies_[index];
      const BodyId id{serial_, static_cast<std::uint32_t>(index),
                      slot.generation};
      if (!slot.live || query.exclude == id ||
          (query.exclude_user_data &&
           *query.exclude_user_data == slot.user_data) ||
          (query.exclude_flags & slot.query_flags) != 0) {
        continue;
      }
      JPH::BodyLockRead lock(system_.GetBodyLockInterface(), slot.body);
      if (!lock.Succeeded()) throw std::logic_error("Overlap lost serial body");
      const auto& body = lock.GetBody();
      if ((query.layers & (1U << body.GetObjectLayer())) == 0 ||
          !bounds.Overlaps(body.GetWorldSpaceBounds())) {
        continue;
      }
      results.receiver(id, slot.shape, slot.user_data);
      JPH::CollisionDispatch::sCollideShapeVsShape(
          &sphere, body.GetShape(), JPH::Vec3::sOne(), JPH::Vec3::sOne(),
          sphere_transform, body.GetCenterOfMassTransform(), {}, {}, settings,
          results);
    }
    for (std::size_t index = 0; index < instances_.size(); ++index) {
      const auto& slot = instances_[index];
      const QueryInstanceId id{serial_, static_cast<std::uint32_t>(index),
                               slot.generation};
      if (!slot.live || (query.layers & (1U << slot.layer)) == 0 ||
          query.exclude_instance == id ||
          (query.exclude_user_data &&
           *query.exclude_user_data == slot.user_data) ||
          (query.exclude_flags & slot.query_flags) != 0 ||
          !bounds.Overlaps(slot.geometry.bounds)) {
        continue;
      }
      const auto& retained = shape(slot.shape);
      if (!retained.mesh) {
        throw std::invalid_argument(
            "Sphere overlap needs an affine mesh instance");
      }
      results.receiver(id, slot.shape, slot.user_data);
      JPH::Shape::GetTrianglesContext context{};
      const auto local_bounds =
          bounds.Transformed(affine_matrix(slot.geometry.inverse));
      retained.shape->GetTrianglesStart(
          context, local_bounds, retained.shape->GetCenterOfMass(),
          JPH::Quat::sIdentity(), JPH::Vec3::sOne());
      constexpr int kBatch = JPH::Shape::cGetTrianglesMinTrianglesRequested;
      constexpr std::size_t kCorners = 3;
      std::array<JPH::Float3, kBatch * kCorners> vertices{};
      JPH::CollideSphereVsTriangles collide(
          &sphere, JPH::Vec3::sOne(), JPH::Vec3::sOne(), sphere_transform,
          JPH::Mat44::sIdentity(), {}, settings, results);
      for (;;) {
        const auto count =
            retained.shape->GetTrianglesNext(context, kBatch, vertices.data());
        if (count == 0) break;
        for (int triangle = 0; triangle < count; ++triangle) {
          std::array<JPH::Vec3, kCorners> points{};
          for (std::size_t corner = 0; corner < kCorners; ++corner) {
            const auto& value = vertices.at(
                static_cast<std::size_t>(triangle) * kCorners + corner);
            points.at(corner) = native(math::transform_point<float>(
                {value.x, value.y, value.z}, slot.geometry.transform));
          }
          constexpr JPH::uint8 kAllEdges = 7;
          collide.Collide(points[0], points[1], points[2], kAllEdges, {});
        }
      }
    }
    return results.count();
  }
  std::size_t body_count() const {
    thread();
    return system_.GetNumBodies();
  }

 private:
  std::shared_ptr<Runtime> runtime_;
  const bw::world::WorldClock& clock_;
  std::thread::id thread_;
  std::uint64_t serial_, last_sequence_{}, solved_sequence_{},
      published_sequence_{};
  BroadLayers broad_layers_;
  BroadFilter broad_filter_;
  PairFilter pair_filter_;
  JPH::TempAllocatorImpl scratch_;
  JPH::GroupFilterTable group_filter_;
  memory::Vector<ShapeSlot> shapes_;
  memory::Vector<BodySlot> bodies_;
  memory::Vector<QueryInstanceSlot> instances_;
  memory::Vector<ConstraintSlot> constraints_;
  memory::Vector<Command> commands_;
  Configuration configuration_;
  JPH::JobSystemSingleThreaded jobs_;
  JPH::PhysicsSystem system_;
};
}  // namespace detail
namespace {
std::shared_ptr<detail::World> borrow(
    const std::weak_ptr<detail::World>& weak) {
  auto world = weak.lock();
  if (!world) {
    throw std::logic_error("Physics capability outlived its world");
  }
  return world;
}
}  // namespace
World Runtime::create_world(const Configuration& configuration,
                            const bw::world::WorldClock& clock) const {
  valid(runtime_ != nullptr, "Moved physics runtime");
  finite(configuration.gravity);
  const auto& limits = configuration.limits;
  constexpr std::uint32_t kMaximumPairTableBodies = 65535;
  valid(limits.bodies > 0 && limits.bodies <= kMaximumPairTableBodies &&
            limits.shapes > 0 && limits.constraints > 0 &&
            limits.body_pairs > 0 && limits.contacts > 0 &&
            limits.commands > 0 && limits.scratch_bytes > 0,
        "Invalid physics capacity");
  valid(configuration.collision_steps > 0 &&
            configuration.collision_steps <=
                static_cast<std::uint32_t>(std::numeric_limits<int>::max()),
        "Invalid physics subdivision");
  return World(std::allocate_shared<detail::World>(
      memory::Allocator<detail::World>(runtime_->domain()), runtime_,
      configuration, clock));
}
World::~World() = default;
ShapeId World::create_shape(const Shape& shape) {
  return world_->create_shape(shape);
}
ShapeId World::create_shape(ShapeId child, const Pose& local) {
  return world_->create_shape(child, local);
}
BodyId World::create_body(const BodyDescription& body) {
  return world_->create_body(body);
}
QueryInstanceId World::create_query_instance(
    const QueryInstanceDescription& instance) {
  return world_->create_query_instance(instance);
}
void World::remove_query_instance(QueryInstanceId id) {
  world_->remove_query_instance(id);
}
QueryInstanceId Resources::create_query_instance(
    const QueryInstanceDescription& instance) const {
  return borrow(world_)->create_query_instance(instance);
}
void Resources::remove_query_instance(QueryInstanceId id) const {
  borrow(world_)->remove_query_instance(id);
}
void Mutations::query_transform(QueryInstanceId id,
                                const math::Affine3<float>& transform) const {
  const QueryTransform value{id, transform};
  query_transforms(std::span(&value, 1));
}
void Mutations::query_flags(std::span<const QueryFlags> flags) const {
  borrow(world_)->query_flags(flags);
}
void Mutations::query_transforms(
    std::span<const QueryTransform> transforms) const {
  borrow(world_)->query_transforms(transforms);
}
ConstraintId World::create_constraint(const DistanceConstraint& constraint) {
  return world_->create_constraint(constraint);
}
ConstraintId World::create_constraint(const HingeConstraint& constraint) {
  return world_->create_constraint(constraint);
}
ConstraintId World::create_constraint(const SwingTwistConstraint& constraint) {
  return world_->create_constraint(constraint);
}
void World::remove_constraint(ConstraintId id) {
  world_->remove_constraint(id);
}
void World::remove_body(BodyId id) { world_->remove_body(id); }
void World::remove_shape(ShapeId id) { world_->remove_shape(id); }
Resources World::resources() { return Resources(world_); }
ShapeId Resources::create_shape(const Shape& shape) const {
  return borrow(world_)->create_shape(shape);
}
ShapeId Resources::create_shape(ShapeId child, const Pose& local) const {
  return borrow(world_)->create_shape(child, local);
}
BodyId Resources::create_body(const BodyDescription& description) const {
  return borrow(world_)->create_body(description);
}
ConstraintId Resources::create_constraint(
    const HingeConstraint& description) const {
  return borrow(world_)->create_constraint(description);
}
ConstraintId Resources::create_constraint(
    const SwingTwistConstraint& description) const {
  return borrow(world_)->create_constraint(description);
}
void Resources::remove_constraint(ConstraintId id) const {
  borrow(world_)->remove_constraint(id);
}
void Resources::remove_body(BodyId id) const {
  borrow(world_)->remove_body(id);
}
void Resources::remove_shape(ShapeId id) const {
  borrow(world_)->remove_shape(id);
}
Queries World::queries() const { return Queries(world_); }
Mutations World::mutations() { return Mutations(world_); }
void World::advance(const bw::world::WorldFrame& frame) {
  world_->advance(frame);
}
void World::publish_queries(const bw::world::WorldFrame& frame) {
  world_->publish_queries(frame);
}
std::size_t World::body_count() const { return world_->body_count(); }
OverlapCount Queries::overlap_sphere(const SphereQuery& query,
                                     std::span<SphereOverlap> output) const {
  return borrow(world_)->overlap_sphere(query, output);
}
std::optional<Hit> Queries::ray(const Query& query) const {
  return borrow(world_)->ray(query);
}
BodyState Queries::body(BodyId id) const { return borrow(world_)->observe(id); }
void Mutations::require_capacity(std::size_t commands) const {
  borrow(world_)->require_capacity(commands);
}
void Mutations::pose(BodyId body, const Pose& pose) const {
  (void)rotation(pose);
  borrow(world_)->queue(
      {.kind = detail::CommandKind::pose, .body = body, .pose = pose});
}
void Mutations::kinematic_target(BodyId body, const Pose& pose) const {
  (void)rotation(pose);
  borrow(world_)->queue({.kind = detail::CommandKind::kinematic_target,
                         .body = body,
                         .pose = pose});
}
void Mutations::velocity(BodyId body, Vector3 linear, Vector3 angular) const {
  finite(linear);
  finite(angular);
  borrow(world_)->queue({.kind = detail::CommandKind::velocity,
                         .body = body,
                         .first = linear,
                         .second = angular});
}
void Mutations::motion(BodyId body, Motion motion) const {
  borrow(world_)->queue(
      {.kind = detail::CommandKind::motion, .body = body, .motion = motion});
}
void Mutations::force(BodyId body, Vector3 force, Vector3 point) const {
  finite(force);
  finite(point);
  borrow(world_)->queue({.kind = detail::CommandKind::force,
                         .body = body,
                         .first = force,
                         .second = point});
}
void Mutations::impulse(BodyId body, Vector3 impulse, Vector3 point) const {
  finite(impulse);
  finite(point);
  borrow(world_)->queue({.kind = detail::CommandKind::impulse,
                         .body = body,
                         .first = impulse,
                         .second = point});
}
}  // namespace bw::physics
