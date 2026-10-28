// Reconstruct the source character physics ownership described in the header.
// Bone names and parent order resolve at preparation; bounded resource rollback
// covers any failure. Frame updates validate all transforms and command
// capacity before queueing. Solved matrices become local bone rotations before
// ordinary SkeletonPose composition, retaining the original root/translation
// rules. Both phases I.11 (#ri-raw) distinguishes borrowed
// skeleton/capabilities from owned provider identities; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at- init.md) sizes all scratch
// before ticking. Both phases I.4 keeps returned animation scale explicit;
// fixed recipe shapes do not inherit that diagnostic scale. No middleware or
// custom solver here.
#include "character_physics.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "bw/math/rigid_transform.hpp"
#include "bw/math/segment.hpp"

namespace bw::game {
namespace {
constexpr std::size_t kNoBody = std::numeric_limits<std::size_t>::max();
constexpr float kRigidTolerance = .001F;
// The legacy SDK receives a matrix but owns a rigid body. Separate affine
// scale at this explicit replacement-provider boundary, following pinned
// Unreal.
math::TransformComponents<float> body_components(
    const animation::Matrix& matrix) {
  const auto components =
      math::decompose_orthogonal_affine(matrix, kRigidTolerance);
  if (!components) {
    throw std::invalid_argument(
        "Character physics requires finite orthogonal affine bone transforms");
  }
  return *components;
}
physics::Pose body_pose(const animation::Matrix& matrix) {
  const auto components = body_components(matrix);
  return {components.translation, components.rotation};
}
animation::Matrix body_matrix(const physics::Pose& pose) {
  return math::homogeneous_transform(math::TransformComponents<float>{
      pose.rotation, pose.position, {1, 1, 1}});
}
struct JointPoses {
  std::span<const animation::Matrix> bind, current;
};
std::array<physics::JointFrame, 2> joint_frames(
    const CharacterPhysicsJoint& joint, JointPoses poses) {
  const auto bind = poses.bind;
  const auto current = poses.current;
  const auto axis =
      math::transform_direction<float>(joint.axis, bind[joint.first]);
  const auto magnitude = math::length(axis);
  if (!std::isfinite(magnitude) || magnitude <= 0) {
    throw std::invalid_argument("Invalid character joint axis");
  }
  const auto direction = math::divide(axis, magnitude);
  constexpr float kParallel = .9F;
  const physics::Vector3 up = std::abs(direction[1]) < kParallel
                                  ? physics::Vector3{0, 1, 0}
                                  : physics::Vector3{1, 0, 0};
  const auto basis = math::affine_part(math::orthonormal_basis(direction, up));
  const auto anchor = math::translation(bind[joint.second]);
  std::array<physics::JointFrame, 2> result{};
  const std::array bodies{joint.first, joint.second};
  for (std::size_t side = 0; side < bodies.size(); ++side) {
    const auto index = bodies.at(side);
    const auto inverse = math::inverse_affine(bind[index]);
    if (!inverse) {
      throw std::invalid_argument("Singular character bind transform");
    }
    // Original builds local joint frames in bind pose, then places bodies at
    // the current animation. Transform each frame independently to preserve
    // that rest pose while creating bodies directly at their current pose.
    const auto placement = math::multiply(*inverse, current[index]);
    result.at(side) = {math::transform_point<float>(anchor, placement),
                       math::transform_direction<float>(direction, placement),
                       math::transform_direction<float>(basis[0], placement)};
  }
  return result;
}
}  // namespace
CharacterPhysics::CharacterPhysics(
    physics::Resources resources, physics::Queries queries,
    physics::Mutations mutations, const CharacterPhysicsRecipe& recipe,
    const formats::SkeletonDefinition& skeleton,
    std::span<const formats::ModelBoneName> names,
    const animation::SkeletonPose& initial, CharacterPhysicsCollision collision,
    const std::shared_ptr<memory::Domain>& domain)
    : resources_(std::move(resources)),
      queries_(std::move(queries)),
      mutations_(std::move(mutations)),
      skeleton_(skeleton),
      locals_(memory::Allocator<formats::SkeletonTransform>(domain)),
      world_(memory::Allocator<animation::Matrix>(domain)),
      order_(memory::Allocator<std::size_t>(domain)),
      body_for_bone_(memory::Allocator<std::size_t>(domain)) {
  const auto& bones = skeleton_.bones();
  if (recipe.body_count == 0 ||
      recipe.body_count > character_physics_capacity ||
      recipe.joint_count > character_physics_capacity ||
      initial.bone_transforms().size() != bones.size()) {
    throw std::invalid_argument("Invalid character physics preparation");
  }
  locals_.assign(initial.local_transforms().begin(),
                 initial.local_transforms().end());
  world_.resize(bones.size());
  body_for_bone_.resize(bones.size(), kNoBody);
  order_.reserve(bones.size());
  // Prepare topological order once, including malformed-cycle rejection.
  while (order_.size() < bones.size()) {
    const auto previous = order_.size();
    for (std::size_t i = 0; i < bones.size(); ++i) {
      if (std::ranges::find(order_, i) != order_.end()) {
        continue;
      }
      const auto parent = bones[i].parent;
      if (parent < 0 ||
          std::ranges::find(order_, static_cast<std::size_t>(parent)) !=
              order_.end()) {
        order_.push_back(i);
      }
    }
    if (previous == order_.size()) {
      throw std::invalid_argument("Cyclic character skeleton");
    }
  }
  std::array<animation::Matrix, character_physics_capacity> transforms{},
      bind_transforms{};
  for (const auto bone : order_) {
    const auto parent = bones[bone].parent;
    const auto& parent_world = parent < 0
                                   ? animation::kIdentityMatrix
                                   : world_[static_cast<std::size_t>(parent)];
    world_[bone] = math::multiply(
        animation::transform_matrix(bones[bone].relative), parent_world);
  }
  for (std::size_t i = 0; i < recipe.body_count; ++i) {
    const auto found = std::ranges::find(names, recipe.bodies.at(i).bone_name(),
                                         &formats::ModelBoneName::name);
    if (found == names.end() || found->bone >= bones.size() ||
        body_for_bone_[found->bone] != kNoBody) {
      throw std::invalid_argument(
          "Character physics bone mapping is incomplete");
    }
    bones_.at(i) = found->bone;
    body_for_bone_[found->bone] = i;
    const auto components =
        body_components(initial.bone_transforms()[found->bone]);
    animation_scales_.at(i) = components.scale;
    transforms.at(i) =
        body_matrix({components.translation, components.rotation});
    bind_transforms.at(i) = body_matrix(body_pose(world_[found->bone]));
    (void)body_pose(transforms.at(i));
  }
  // Only this instance's resources are published; rollback retains the world.
  try {
    for (std::size_t i = 0; i < recipe.body_count; ++i) {
      const auto& source = recipe.bodies.at(i);
      const auto primitive =
          source.geometry == CharacterPhysicsBody::Geometry::sphere
              ? resources_.create_shape(physics::Sphere{source.radius})
              : resources_.create_shape(physics::Box{source.half_extent});
      primitive_shapes_.at(primitive_count_++) = primitive;
      const auto placed =
          resources_.create_shape(primitive, {.position = source.local_center});
      placed_shapes_.at(placed_count_++) = placed;
      physics::BodyDescription description;
      description.shape = placed;
      description.pose = body_pose(transforms.at(i));
      description.motion = physics::Motion::kinematic;
      description.layer = collision.layer;
      description.material = collision.material;
      description.mass = {source.mass, {}, physics::Inertia::from_shape};
      description.linear_damping = source.linear_damping;
      description.angular_damping = source.angular_damping;
      description.user_data = collision.user_data;
      bodies_.at(body_count_++) = resources_.create_body(description);
    }
    for (std::size_t i = 0; i < recipe.joint_count; ++i) {
      const auto& source = recipe.joints.at(i);
      if (source.first >= body_count_ || source.second >= body_count_) {
        throw std::invalid_argument("Invalid character body joint");
      }
      const auto frames =
          joint_frames(source, {std::span(bind_transforms).first(body_count_),
                                std::span(transforms).first(body_count_)});
      physics::ConstraintId created;
      if (source.kind == CharacterPhysicsJoint::Kind::hinge) {
        created = resources_.create_constraint(physics::HingeConstraint{
            bodies_.at(source.first), bodies_.at(source.second), frames[0],
            frames[1], source.minimum_angle, source.maximum_angle});
      } else {
        created = resources_.create_constraint(physics::SwingTwistConstraint{
            bodies_.at(source.first),
            bodies_.at(source.second),
            frames[0],
            frames[1],
            source.swing_angle,
            source.minimum_angle,
            source.maximum_angle,
            {source.swing_stiffness, source.swing_damping},
            {source.twist_stiffness, source.twist_damping}});
      }
      joints_.at(joint_count_++) = created;
    }
  } catch (...) {
    retire();
    throw;
  }
}
CharacterPhysics::~CharacterPhysics() { retire(); }
void CharacterPhysics::retire() {
  if (!resources_.alive()) {
    return;
  }
  while (joint_count_ > 0) {
    resources_.remove_constraint(joints_.at(--joint_count_));
  }
  while (body_count_ > 0) {
    resources_.remove_body(bodies_.at(--body_count_));
  }
  while (placed_count_ > 0) {
    resources_.remove_shape(placed_shapes_.at(--placed_count_));
  }
  while (primitive_count_ > 0) {
    resources_.remove_shape(primitive_shapes_.at(--primitive_count_));
  }
}
void CharacterPhysics::follow_animation(const animation::SkeletonPose& pose,
                                        bool reset) {
  if (physical_) {
    throw std::logic_error("Animation cannot move physical character bodies");
  }
  if (pose.bone_transforms().size() != world_.size()) {
    throw std::invalid_argument("Character pose size mismatch");
  }
  std::array<physics::Pose, character_physics_capacity> targets{};
  std::array<physics::Vector3, character_physics_capacity> scales{};
  for (std::size_t i = 0; i < body_count_; ++i) {
    const auto components =
        body_components(pose.bone_transforms()[bones_.at(i)]);
    targets.at(i) = {components.translation, components.rotation};
    scales.at(i) = components.scale;
  }
  mutations_.require_capacity(body_count_);
  for (std::size_t i = 0; i < body_count_; ++i) {
    if (reset) {
      mutations_.pose(bodies_.at(i), targets.at(i));
    } else {
      mutations_.kinematic_target(bodies_.at(i), targets.at(i));
    }
  }
  animation_scales_ = scales;
  std::ranges::copy(pose.local_transforms(), locals_.begin());
}
void CharacterPhysics::handoff() {
  if (physical_) {
    return;
  }
  mutations_.require_capacity(body_count_ + (pending_impulse_ ? 1 : 0));
  for (std::size_t i = 0; i < body_count_; ++i) {
    mutations_.motion(bodies_.at(i), physics::Motion::dynamic);
  }
  physical_ = true;
  if (pending_impulse_) {
    apply_impulse(*pending_impulse_);
    pending_impulse_.reset();
  }
}
bool CharacterPhysics::receive_corpse_shot(physics::Vector3 direction,
                                           physics::Vector3 point) {
  if (!physical_) return false;
  constexpr float kOrdinaryCorpseMagnitude = 2;
  return request_impulse({direction, point, kOrdinaryCorpseMagnitude, 1, -1});
}
bool CharacterPhysics::receive_corpse_melee(physics::Vector3 center,
                                            physics::Vector3 point,
                                            float radius) {
  if (!physical_) return false;
  if (!math::all_finite(center) || !math::all_finite(point) ||
      !std::isfinite(radius) || radius < 0) {
    throw std::invalid_argument("Invalid corpse melee geometry");
  }
  constexpr float kVerticalScale = .5F, kMagnitude = 4;
  auto direction = math::subtract(point, center);
  direction[1] = (direction[1] + radius * kVerticalScale) * kVerticalScale;
  return request_impulse({direction, point, kMagnitude, 1, 1});
}
bool CharacterPhysics::request_impulse(const CharacterImpulse& request) {
  const float length = math::length(request.direction);
  if (!std::isfinite(length) || length <= 0 ||
      !std::isfinite(request.magnitude) || request.magnitude < 0 ||
      !std::ranges::all_of(
          request.point, [](float v) { return std::isfinite(v); }) ||
      request.body >= static_cast<std::int32_t>(body_count_)) {
    throw std::invalid_argument("Invalid source character impulse");
  }
  constexpr float kSourceImpulseScale = 30.F;
  const PendingImpulse candidate{
      math::scale(math::scale(request.direction, 1.F / length),
                  request.magnitude * kSourceImpulseScale),
      request.point, request.priority, request.body};
  if (!math::all_finite(candidate.value)) {
    throw std::overflow_error("Source character impulse overflow");
  }
  if (physical_) {
    apply_impulse(candidate);
    return true;
  }
  if (pending_impulse_ &&
      (candidate.priority < pending_impulse_->priority ||
       (candidate.priority == pending_impulse_->priority &&
        math::dot(candidate.value, candidate.value) <
            math::dot(pending_impulse_->value, pending_impulse_->value)))) {
    return false;
  }
  pending_impulse_ = candidate;
  return true;
}
void CharacterPhysics::apply_impulse(const PendingImpulse& request) {
  auto selected = request.body;
  if (selected < 0) {
    constexpr float kSourceSelectionLength = 100.F;
    const auto end = math::add(
        request.point, math::scale(request.value, kSourceSelectionLength));
    float closest = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < body_count_; ++i) {
      // All recipe bodies transfer together. No independently kinematic body
      // remains eligible for exclusion after this adapter's handoff.
      const auto position = queries_.body(bodies_.at(i)).pose.position;
      const float distance =
          math::point_segment_distance_squared(position, request.point, end);
      if (distance < closest) {
        closest = distance;
        selected = static_cast<std::int32_t>(i);
      }
    }
  }
  if (selected < 0) throw std::logic_error("Character impulse has no body");
  mutations_.impulse(bodies_.at(static_cast<std::size_t>(selected)),
                     request.value, request.point);
}
void CharacterPhysics::impulse(std::size_t body, physics::Vector3 value,
                               physics::Vector3 point) {
  if (!physical_ || body >= body_count_) {
    throw std::invalid_argument(
        "Impulse requires a selected physical character body");
  }
  mutations_.impulse(bodies_.at(body), value, point);
}
animation::Matrix CharacterPhysics::publish(
    animation::SkeletonPose& destination) {
  if (!physical_) {
    throw std::logic_error("Physical pose requested before handoff");
  }
  std::array<physics::Pose, character_physics_capacity> solved{};
  for (std::size_t i = 0; i < body_count_; ++i) {
    const auto state = queries_.body(bodies_.at(i));
    if (state.motion != physics::Motion::dynamic) {
      throw std::logic_error("Physical pose requires admitted world handoff");
    }
    solved.at(i) = state.pose;
  }
  const auto& bones = skeleton_.bones();
  const auto pelvis = bones_.at(0);
  for (auto ancestor = static_cast<std::int32_t>(pelvis); ancestor >= 0;
       ancestor = bones[static_cast<std::size_t>(ancestor)].parent) {
    locals_[static_cast<std::size_t>(ancestor)] = {{0, 0, 0, 1}, {}, {1, 1, 1}};
  }
  for (const auto bone : order_) {
    const auto parent = bones[bone].parent;
    const auto& parent_world = parent < 0
                                   ? animation::kIdentityMatrix
                                   : world_[static_cast<std::size_t>(parent)];
    const auto body = body_for_bone_[bone];
    if (body != kNoBody) {
      const auto inverse = math::inverse_affine(parent_world);
      if (!inverse) {
        throw std::runtime_error("Singular physical pose parent");
      }
      const auto local = math::multiply(body_matrix(solved.at(body)), *inverse);
      const auto rigid = body_pose(local);
      locals_[bone] = {
          rigid.rotation,
          body == 0 ? rigid.position : bones[bone].relative.translation,
          {1, 1, 1}};
    }
    world_[bone] = math::multiply(animation::transform_matrix(locals_[bone]),
                                  parent_world);
  }
  const auto model_root = world_[pelvis];
  locals_[pelvis] = {{0, 0, 0, 1}, {}, {1, 1, 1}};
  destination.evaluate_locals(locals_, model_root);
  return model_root;
}
}  // namespace bw::game
