// Grounded action admission and entry effects apply both-phase I.1
// (#ri-explicit) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md): explicit one-shot
// synchronous source effects, fixed storage, no independent clock or
// allocation. Exclusive accepted base-action owner above PlayerActor's prepared
// playback. Borrow the actor at a stable address for this controller-owned
// lifetime; neither appearance preparation nor pause replaces/ticks this owner.
// One game thread; all policies and pending requests are fixed values, no hot
// allocation. Preflight catalog admission, apply a source plan, then publish
// policy state. Observe current clocks/CAN0 before the controller advances
// motion, derive transitions and return physical/scoped-capability obligations.
// Never advance animation clocks, create damage/spells or substitute a
// dynamic-body solver. Invalid inputs throw before mutation. Unavailable
// resources and missing RNG report explicit outcomes; they preserve existing
// policy/pose state. Callers must fulfill returned magic scope changes and
// dynamic handoff requirements. Controller separately validates airborne/state
// replacement admission and suppresses ordinary locomotion while
// blocks_locomotion() is true, but keeps root/contact/pose/event updates
// running. External body replacement requires interrupt(); unexpected
// generation changes are reported, not overwritten. Both MCP phases: I.11
// (#ri-raw) preserves actor borrowing; E.4 protects admission invariants; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds policy storage.
// Entry effects call a borrowed controller-owned sink synchronously in source
// order. No delayed acknowledgement, artificial admission limit or allocation.
// See alicia-action-transitions.md for source ordering.
#pragma once

#include "alicia_reaction_policy.hpp"
#include "spell_presentation_policy.hpp"

namespace bw::game {
class PlayerActor;
enum class PlayerMovementPhase : std::uint8_t;
enum class AliciaActionKind : std::uint8_t {
  none,
  damage,
  sustained_damage,
  close_attack,
  magic,
  death
};
enum class AliciaActionOutcome : std::uint8_t {
  unchanged,
  started,
  completed,
  interrupted,
  unavailable_motion,
  unsupported_source_state,
  random_required,
  observation_required,
  dynamic_body_required,
  recovery_state_required,
  body_owner_changed
};
struct AliciaActionUpdate {
  AliciaActionOutcome outcome{AliciaActionOutcome::unchanged};
  std::optional<math::Vector3<float>> facing_target;
  std::optional<std::uint32_t> cancelled_slot;
  bool return_crouched{};
  std::optional<AcceptedMagicPresentation> acquire_magic_scope,
      release_magic_scope;
  std::optional<SpellPresentationScope> acquire_spell_scope,
      release_spell_scope;
  std::optional<bool> perform_root_contact;
};
struct AliciaActionStep {
  double seconds{};
  std::optional<float> random_unit;
  bool support_lost{}, death_handoff_blocked{};
  bool source_force_idle{}, source_low_health_recovery{};
  SpellPresentationObservation spell{};
};
// Synchronous game-owned source message consumer. The controller and targeting
// owner outlive the action owner; callbacks cannot throw, advance clocks or
// publish model poses. Ordinary-camera requests remain distinct from spell
// scope.
class ShooterEntrySink {
 public:
  virtual void force_shooter_posture(bool ordinary_camera) noexcept = 0;

 protected:
  ShooterEntrySink() = default;
  ~ShooterEntrySink() = default;
  ShooterEntrySink(const ShooterEntrySink&) = default;
  ShooterEntrySink& operator=(const ShooterEntrySink&) = default;
  ShooterEntrySink(ShooterEntrySink&&) = default;
  ShooterEntrySink& operator=(ShooterEntrySink&&) = default;
};
class AliciaActionOwner {
 public:
  // Borrow the containing controller's actual phase, not a mirrored latch.
  // Both references outlive this owner. Non-grounded admission is unsupported;
  // an empty exclusive owner in grounded phase is ordinary standing/crouch.
  AliciaActionOwner(PlayerActor& actor, const PlayerMovementPhase& phase,
                    ShooterEntrySink& shooter) noexcept
      : actor_(&actor), phase_(&phase), shooter_(&shooter) {}
  ~AliciaActionOwner() = default;
  AliciaActionOwner(const AliciaActionOwner&) = delete;
  AliciaActionOwner& operator=(const AliciaActionOwner&) = delete;
  AliciaActionOwner(AliciaActionOwner&&) = delete;
  AliciaActionOwner& operator=(AliciaActionOwner&&) = delete;
  AliciaActionUpdate begin_damage(const AcceptedDamageReaction& accepted);
  AliciaActionUpdate begin_sustained_damage(
      const AcceptedSustainedDamageReaction& accepted);
  AliciaActionUpdate sustained_impact(
      const AcceptedSustainedDamageReaction& accepted);
  AliciaActionUpdate begin_close_attack(CloseAttackVariant variant);
  AliciaActionUpdate queue_close_attack(CloseAttackVariant variant,
                                        float trigger_frame);
  AliciaActionUpdate begin_magic(const AcceptedMagicPresentation& accepted);
  AliciaActionUpdate begin_spell(SpellPresentationKind accepted);
  AliciaActionUpdate accept_thunder_target();
  AliciaActionUpdate finish_thunder_aim();
  // Complete the source phase after physical root/contact, before pose output.
  // Only source-marked requests are deferred; no motion clocks advance here.
  void finish_physical_step();
  AliciaActionUpdate begin_death(const AcceptedDeathPresentation& accepted);
  AliciaActionUpdate advance_before_motion(const AliciaActionStep& step);
  AliciaActionUpdate interrupt() noexcept;
  AliciaActionKind kind() const noexcept { return kind_; }
  // Timed update is a distinct registration from controller-owned hardware
  // and posture-message admission; three source spell states replace it.
  bool updates_shooter() const noexcept;
  // Read the current source body-model scope; lighting/visibility queries do
  // not obtain action mutation authority. Weapons have independent flags.
  bool forces_model_visibility() const noexcept;
  bool blocks_locomotion() const noexcept {
    return kind_ != AliciaActionKind::none;
  }

 private:
  bool available(const ActionMotionPlan& plan) const;
  void publish(const ActionMotionPlan& plan);
  // Applies the outgoing ordinary state's upper-bank exit before entry.
  // Intra-action/followup publication uses publish() and does not repeat exit.
  void publish_entry(ActionMotionPlan plan,
                     std::optional<bool> force_before_body = {});
  bool grounded_entry() const noexcept;
  void remember_body();
  AliciaActionUpdate commit_spell_transition(
      SpellPresentationPolicy candidate, const SpellPresentationUpdate& update);
  PlayerActor* actor_;
  const PlayerMovementPhase* phase_;
  ShooterEntrySink* shooter_;
  DamageReaction damage_;
  SustainedDamageReaction sustained_;
  CloseAttackPresentation close_attack_;
  MagicPresentation magic_;
  std::optional<SpellPresentationPolicy> spell_;
  std::optional<SpellPresentationScope> spell_scope_;
  ActionMotionPlan post_contact_motion_;
  AliciaActionKind kind_{AliciaActionKind::none};
  std::optional<float> followup_frame_;
  std::optional<std::uint64_t> body_generation_;
  bool dynamic_handoff_{}, recovery_handoff_{};
};
}  // namespace bw::game
