// Implement the build-selected game preview capability using existing owners.
// All borrows remain inside this adapter; tools receive only const observations
// and bounded operations. The world creates the adapter after simulation and
// presentation, and retires it first. Reset/step/publish ordering is unchanged.
// Core I.1 (#ri-explicit), R.3 (#rr-ptr) and performance TLM.1
// (guidelines/telemetry/TLM.1-compile-telemetry-out-by-default.md), retrieved
// for both phases, require explicit authority and complete OFF target
// exclusion. One allocator-backed owner/control block at setup, no steady-frame
// allocations.
#include <stdexcept>

#include "bw/memory/allocator.hpp"
#include "game/world/animation_preview.hpp"
#include "game/world/mission_simulation.hpp"
#include "player_animation_review.hpp"

namespace bw::game {
// Check each command's required owner at this boundary, rather than relying on
// a caller's earlier preview-admission observation. No mutable borrow escapes.
PlayerActor& AnimationPreviewAccess::require_actor() const {
  auto& actor = owner_.get().player_;
  if (!actor.has_value()) {
    throw std::logic_error("Animation preview requires an admitted actor");
  }
  return *actor;
}
PlayerController& AnimationPreviewAccess::require_controller() const {
  auto& controller = owner_.get().controller_;
  if (!controller.has_value()) {
    throw std::logic_error("Animation preview requires physical traversal");
  }
  return *controller;
}
const PlayerActor* AnimationPreviewAccess::actor() const {
  return owner_.get().actor();
}
const PlayerMovementState* AnimationPreviewAccess::movement() const {
  return owner_.get().player_state();
}
const AliciaActionUpdate* AnimationPreviewAccess::outcome() const {
  return owner_.get().player_action_status();
}
AliciaActionKind AnimationPreviewAccess::action_kind() const {
  return require_controller().action_owner().kind();
}
bool AnimationPreviewAccess::blocks_locomotion() const {
  return require_controller().action_owner().blocks_locomotion();
}
bool AnimationPreviewAccess::paused() const { return owner_.get().paused(); }
unsigned AnimationPreviewAccess::weapon_class() const {
  return owner_.get().weapon_class_;
}
void AnimationPreviewAccess::restore(const PlayerMovementState& state) {
  auto& controller = require_controller();
  auto& actor = require_actor();
  controller.restore_review_state(state);
  actor.reset_animation_presentation();
}
void AnimationPreviewAccess::publish_pose() {
  const auto& controller = require_controller();
  auto& actor = require_actor();
  actor.publish_pose(controller.state().pose, controller.state().orientation,
                     controller.body_radius(), publisher_.get());
}
void AnimationPreviewAccess::select_target(bool selected) {
  owner_.get().targeting_.select_for_review(selected);
}
void AnimationPreviewAccess::lower_weapon() {
  require_controller().lower_review_weapon();
}
AliciaActionUpdate AnimationPreviewAccess::accept_thunder_target() {
  return require_controller().action_owner().accept_thunder_target();
}
AliciaActionUpdate AnimationPreviewAccess::begin_jump(
    const AcceptedScriptedJump& request) {
  return require_controller().begin_scripted_jump(request);
}
AliciaActionUpdate AnimationPreviewAccess::begin_damage(
    const AcceptedDamageReaction& request) {
  return require_controller().action_owner().begin_damage(request);
}
AliciaActionUpdate AnimationPreviewAccess::begin_death(
    const AcceptedDeathPresentation& request) {
  return require_controller().action_owner().begin_death(request);
}
AliciaActionUpdate AnimationPreviewAccess::begin_spell(
    SpellPresentationKind kind) {
  return require_controller().action_owner().begin_spell(kind);
}
AliciaActionUpdate AnimationPreviewAccess::begin_melee(
    CloseAttackVariant kind) {
  return require_controller().action_owner().begin_close_attack(kind);
}
animation::PlaybackAdmission AnimationPreviewAccess::request_animation(
    CharacterMotionBank bank, const CharacterMotionRequest& request) {
  return require_actor().request_animation(bank, request);
}
bool AnimationPreviewAccess::facial(std::size_t index) {
  return require_actor().preview_facial(index);
}
ActionPresentationAdmission AnimationPreviewAccess::apply(
    const ActionMotionPlan& plan) {
  return require_actor().apply_action_plan(plan);
}
void AnimationPreviewAccess::advance_fixed(double seconds) {
  auto& actor = require_actor();
  const auto& controller = require_controller();
  (void)actor.advance_motion(seconds);
  actor.publish_pose(controller.state().pose, controller.state().orientation,
                     controller.body_radius(), publisher_.get());
}
double AnimationPreviewAccess::advance(PlayerMovementInput movement, float yaw,
                                       double seconds,
                                       PlayerActionObservations observations) {
  return require_controller().advance(movement, yaw, seconds, publisher_.get(),
                                      observations);
}
std::shared_ptr<AnimationPreview> make_animation_preview(
    AnimationPreviewAccess access,
    const formats::CustomizationDefinition& customization,
    const std::shared_ptr<memory::Domain>& domain) {
  return std::allocate_shared<PlayerAnimationReview>(
      memory::Allocator<PlayerAnimationReview>(domain), access, customization);
}
}  // namespace bw::game
