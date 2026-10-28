// Shared source character damage/death animation policy. Callers admit hits,
// supply copied model bases, authored weights and random samples, then apply
// returned plans through their own catalogs. Health, physics and clocks stay
// with actor/world owners. Damage completion and physical handoff are explicit
// results, never implicit idle or a fake death. Fixed storage, no allocation;
// invalid observations throw before mutation. Original behavior is unchanged
// from the accepted player policy; EA0 uses the same native receiver family.
// Both-phase Core I.11 (#ri-raw) prevents ownership escape; performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// shares copied policy without coupling NPCs to Alicia's controller.
#pragma once
#include "action_motion_plan.hpp"
#include "bw/math/vector.hpp"
namespace bw::game {
struct ReactionDirection {
  math::Vector3<float> incoming{};
  math::Vector3<float> actor_forward{0, 0, 1};
  // Original model +0x70 basis. Caller must not silently substitute the
  // model +0xc0 forward basis used only for a degenerate incoming direction.
  math::Vector3<float> comparison_axis{0, 0, 1};
};
enum class DamageImpactClass : std::uint8_t { ordinary, heavy, crouched };
struct AcceptedDamageReaction {
  ReactionDirection direction;
  math::Vector3<float> comparison_right{1, 0, 0};  // Original model+0x50.
  DamageImpactClass impact_class{DamageImpactClass::ordinary};
};
struct DamageCompletion {
  bool motion_completed{}, motion_repeating{}, cancellation_open{};
  bool source_force_idle{}, source_low_health_recovery{};
};
struct DamageReactionUpdate {
  ActionMotionPlan motion;
  bool active{}, return_crouched{}, requires_recovery_state{};
  std::optional<std::uint32_t> cancelled_slot;
};
class DamageReaction {
 public:
  ActionMotionPlan begin(const AcceptedDamageReaction& accepted);
  DamageReactionUpdate advance(const DamageCompletion& observation);
  void interrupt() noexcept { active_ = false; }

 private:
  std::uint32_t slot_{};
  bool crouched_{}, active_{};
};
// Rare sustained-hit owner, distinct from ordinary StateDamageReaction.
// Entry918/919 is absent from PW0; an Alicia adapter must reject it explicitly.
struct AcceptedSustainedDamageReaction {
  ReactionDirection direction;
  float recovery_seconds{};  // Authored player body +0x158.
  float random_unit{};       // Original RNG sample in [0,1].
};
struct SustainedDamageStep {
  float seconds{};
  bool crossed_frame_ten{};  // Current body owner's original frame crossing.
  float random_unit{};       // Used only when crossing restarts sustained pose.
};
struct ReactionUpdate {
  ActionMotionPlan motion;
  math::Vector3<float> facing_target{};
  bool active{};
};
class SustainedDamageReaction {
 public:
  ReactionUpdate begin(const AcceptedSustainedDamageReaction& accepted);
  // A later accepted hit resets recovery, but not the sustain timer; identical
  // selected slots retain their current playback phase.
  ReactionUpdate impact(const AcceptedSustainedDamageReaction& accepted);
  ReactionUpdate advance(const SustainedDamageStep& step);
  void interrupt() noexcept;
  bool active() const noexcept { return active_; }

 private:
  ReactionUpdate snapshot(ActionMotionPlan plan = {}) const;
  math::Vector3<float> direction_{};
  float recovery_{}, sustain_{};
  std::uint32_t slot_{};
  bool active_{};
};

inline constexpr std::size_t kNonpositiveDeathChoices = 6;
struct AcceptedDeathPresentation {
  ReactionDirection direction;
  // Source current StateSquat observation (6026b0/600f80). Grapple
  // explicitly returns false even if a stale crouch flag remains set.
  bool crouched_state{};
  std::array<float, kNonpositiveDeathChoices>
      nonpositive_weights{};                // Body+0x124..0x138.
  std::array<float, 3> positive_weights{};  // Body+0x13c..0x144.
  float random_unit{};
};
ActionMotionPlan accepted_death(const AcceptedDeathPresentation& accepted);
struct AcceptedMeleeDeathPresentation {
  ReactionDirection direction;
  std::array<float, 2> weights{};
  float random_unit{};
};
// Source6881a0 chooses two close-impact slots, without the crouch override of
// ordinary bullet death. Same fixed lifetime/handoff owner consumes the plan.
ActionMotionPlan accepted_melee_death(const AcceptedMeleeDeathPresentation&);

struct DeathMotionObservation {
  bool motion_completed{};
  bool motion_repeating{};
  bool source_handoff_blocked{};  // Source actor flag0x100.
  bool contact_lost{};
};
// Original death may hand off a repeating or completed motion, or lose
// support. This signals an unresolved dynamic-body capability, never idle.
bool death_requires_dynamic_handoff(
    const DeathMotionObservation& observation) noexcept;

}  // namespace bw::game
