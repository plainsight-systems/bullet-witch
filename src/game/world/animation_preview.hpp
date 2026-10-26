// Build-selected animation preview boundary, not a gameplay action authority.
// WorldRuntime alone creates the bounded capability over its stable simulation
// and update-only publisher. The developer owner borrows it until world
// teardown; no mutable actor/controller/targeting reference escapes. Requests
// observe const catalogs/state and invoke existing action/pose operations on
// the game thread. Calls cannot create equipment, change a clock or write a
// profile. World stepping invokes preview advance only for an active session,
// otherwise its ordinary path. No frame allocations; the optional owner is
// allocated once from the mission domain and destroyed before its borrowers.
// OFF builds omit interface and state. Both phases: Core I.1 (#ri-explicit),
// R.3 (#rr-ptr) expose bounded operations and nonowning lifetime; performance
// TLM.1 (guidelines/telemetry/ TLM.1-compile-telemetry-out-by-default.md)
// requires compile-time exclusion. See developer-compilation-boundary.md for
// pinned Unreal and artifact checks. Nonnull reference_wrapper borrows are
// private; commands reject missing optional physical owners before mutation.
#pragma once
#if BW_DEVELOPER_VIEWS
#include <functional>

#include "animation_review.hpp"
#include "game/actors/alicia/player_actor.hpp"
#include "game/actors/alicia/player_controller.hpp"

namespace bw::game {
class MissionSimulation;
class WorldRuntime;
class AnimationPreviewAccess {
 public:
  const PlayerActor* actor() const;
  const PlayerMovementState* movement() const;
  const AliciaActionUpdate* outcome() const;
  AliciaActionKind action_kind() const;
  bool blocks_locomotion() const;
  bool paused() const;
  unsigned weapon_class() const;
  void restore(const PlayerMovementState&);
  void publish_pose();
  void select_target(bool);
  void lower_weapon();
  AliciaActionUpdate accept_thunder_target();
  AliciaActionUpdate begin_jump(const AcceptedScriptedJump&);
  AliciaActionUpdate begin_damage(const AcceptedDamageReaction&);
  AliciaActionUpdate begin_death(const AcceptedDeathPresentation&);
  AliciaActionUpdate begin_spell(SpellPresentationKind);
  AliciaActionUpdate begin_melee(CloseAttackVariant);
  animation::PlaybackAdmission request_animation(CharacterMotionBank,
                                                 const CharacterMotionRequest&);
  bool facial(std::size_t);
  ActionPresentationAdmission apply(const ActionMotionPlan&);
  void advance_fixed(double);
  double advance(PlayerMovementInput, float, double, PlayerActionObservations);

 private:
  friend class WorldRuntime;
  AnimationPreviewAccess(MissionSimulation& owner,
                         render::ActorPublisher& publisher)
      : owner_(owner), publisher_(publisher) {}
  // Checked internal access never escapes this capability. Missing physical
  // owners reject locally before commands mutate a partially admitted world.
  PlayerActor& require_actor() const;
  PlayerController& require_controller() const;
  std::reference_wrapper<MissionSimulation> owner_;
  std::reference_wrapper<render::ActorPublisher> publisher_;
};
class AnimationPreview {
 public:
  AnimationPreview() = default;
  virtual ~AnimationPreview() = default;
  AnimationPreview(const AnimationPreview&) = delete;
  AnimationPreview& operator=(const AnimationPreview&) = delete;
  AnimationPreview(AnimationPreview&&) = delete;
  AnimationPreview& operator=(AnimationPreview&&) = delete;
  virtual AnimationReviewResult review(AnimationReviewRequest) = 0;
  virtual std::string_view review_status() const = 0;
  virtual bool active() const = 0;
  virtual bool blocks_shooter() const = 0;
  virtual double advance(PlayerMovementInput, float, double,
                         PlayerActionObservations) = 0;
  virtual void clear_after_equipment() = 0;
};
// Only the developer target defines this factory. Private shared storage uses
// the existing allocator-aware control block; no second owner escapes the
// world.
std::shared_ptr<AnimationPreview> make_animation_preview(
    AnimationPreviewAccess, const formats::CustomizationDefinition&,
    const std::shared_ptr<memory::Domain>&);
}  // namespace bw::game
#endif
