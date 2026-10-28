// Own the original character's skeletal physics resources and authority
// handoff. Construction borrows a source recipe, skeleton/names and evaluated
// animation; copies mapping and scratch, creates initially kinematic source
// bodies/joints. Skeleton lifetime must exceed this owner. World capabilities
// are weak and never expose advancement. Destruction retires joints, bodies,
// wrappers then primitive shapes; an expired world has already retired them. No
// copying or moving a live owner. Invalid preparation throws and rolls back its
// resources. Before handoff, follow_animation queues targets and freezes the
// latest local pose. Handoff queues dynamic motion once; game death policy
// chooses its timing and the winning impulse/body. After World advances,
// publish composes solved rotations against skeletal parents, retains authored
// nonroot translations, promotes pelvis to model root and evaluates the final
// pose. No live bullet hit filtering, health, actor movement or own clock
// occurs here. Setup O(bones squared + recipe); updates O(bones + bodies),
// preallocated domain scratch. Core I.11 (#ri-raw) keeps skeleton borrows
// distinct from owned resource identities; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps frame work
// allocation-free. Physical animation frames separate scale using shared
// pinned-Unreal decomposition. Fixed recipe geometry remains authoritative;
// latest extracted scales are retained as diagnostics, not applied to shapes.
// Both phases I.4 (#ri-typed) keeps that choice explicit. Legacy SDK numerical
// equivalence is unresolved; EHIT retains its independent full affine path.
// Source and solver boundary:
// research/engine/subsystems/first-combat-physics-boundary.md.
#pragma once
#include "bw/animation/pose.hpp"
#include "bw/content/formats/model.hpp"
#include "bw/physics/world.hpp"
#include "game/content/original/character_physics_recipe.hpp"

namespace bw::game {
struct CharacterPhysicsCollision {
  physics::Material material;
  std::uint64_t user_data{};
  std::uint8_t layer{};
};
struct CharacterImpulse {
  physics::Vector3 direction{}, point{};
  float magnitude{};
  std::int32_t priority{}, body{-1};
};
class CharacterPhysics {
 public:
  CharacterPhysics(physics::Resources resources, physics::Queries queries,
                   physics::Mutations mutations,
                   const CharacterPhysicsRecipe& recipe,
                   const formats::SkeletonDefinition& skeleton,
                   std::span<const formats::ModelBoneName> names,
                   const animation::SkeletonPose& initial,
                   CharacterPhysicsCollision collision,
                   const std::shared_ptr<memory::Domain>& domain);
  ~CharacterPhysics();
  CharacterPhysics(const CharacterPhysics&) = delete;
  CharacterPhysics& operator=(const CharacterPhysics&) = delete;
  CharacterPhysics(CharacterPhysics&&) = delete;
  CharacterPhysics& operator=(CharacterPhysics&&) = delete;
  void follow_animation(const animation::SkeletonPose& pose,
                        bool reset = false);
  void handoff();
  // Retain the source winning force while animation owns bodies. Higher
  // priority wins; equal priority keeps the larger squared impulse (ties
  // replace). Once physical, queue directly. Negative body chooses the closest
  // body to the application segment; nonnegative out-of-range rejects.
  // Direction must be finite/nonzero, magnitude finite and nonnegative.
  bool request_impulse(const CharacterImpulse&);
  // Ordinary corpse receipt66e7b0: only physical bodies receive source
  // magnitude2, priority1 and nearest-body selection. No living-body force.
  bool receive_corpse_shot(physics::Vector3 direction, physics::Vector3 point);
  // Source685170 uses the biased radial direction, magnitude4 and body1.
  bool receive_corpse_melee(physics::Vector3 center, physics::Vector3 point,
                            float radius);
  void impulse(std::size_t body, physics::Vector3 value,
               physics::Vector3 point);
  // Requires dynamic ownership. Returns the source-promoted model root.
  animation::Matrix publish(animation::SkeletonPose& destination);
  bool physical() const noexcept { return physical_; }
  std::span<const physics::Vector3> animation_scales() const {
    return std::span(animation_scales_).first(body_count_);
  }
  std::span<const physics::BodyId> bodies() const {
    return std::span(bodies_).first(body_count_);
  }

 private:
  void retire();
  struct PendingImpulse {
    physics::Vector3 value{}, point{};
    std::int32_t priority{}, body{-1};
  };
  void apply_impulse(const PendingImpulse&);
  std::optional<PendingImpulse> pending_impulse_;
  physics::Resources resources_;
  physics::Queries queries_;
  physics::Mutations mutations_;
  const formats::SkeletonDefinition& skeleton_;
  memory::Vector<formats::SkeletonTransform> locals_;
  memory::Vector<animation::Matrix> world_;
  memory::Vector<std::size_t> order_, body_for_bone_;
  std::array<std::size_t, character_physics_capacity> bones_{};
  std::array<physics::Vector3, character_physics_capacity> animation_scales_{};
  std::array<physics::BodyId, character_physics_capacity> bodies_{};
  std::array<physics::ConstraintId, character_physics_capacity> joints_{};
  std::array<physics::ShapeId, character_physics_capacity> primitive_shapes_{},
      placed_shapes_{};
  std::size_t body_count_{}, joint_count_{}, primitive_count_{},
      placed_count_{};
  bool physical_{};
};
}  // namespace bw::game
