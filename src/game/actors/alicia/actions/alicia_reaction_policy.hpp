// Hardware melee selection applies both-phase I.1 (#ri-explicit) to the
// caller-owned admission/basis contract and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) to fixed shared-math computations.
// Accepted Alicia reaction and action presentation, separated from combat
// admission. The game owner supplies source direction bases, authored timers,
// random samples and current-owner observations; these policies produce bounded
// ActionMotionPlans. They never create damage, health, spell effects or
// ragdolls. All state is owned by value, single simulation thread, no borrowed
// resources. Validate inputs before mutation; interruption clears only this
// policy state. Ordinary damage uses completion/CAN0; rare sustained hits use
// recovery/sustain timers. Close attacks use source
// completion/CAN0 and follow-up crossings, magic uses its explicit completion
// flag. Death selection preserves authored weighted pools and reports dynamic
// handoff. O(1), no allocation in valid requests/updates. The adapter owns
// presentation resource admission, grounding, facing, events and scoped effect
// restoration. Both MCP design corpora: I.4 (#ri-typed) distinguishes accepted
// observations; E.4 (#re-design-invariants) preserves state on rejection; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds value storage.
// Source contracts and remaining gameplay boundaries:
// alicia-action-transitions.md.
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "alicia_action_policy.hpp"
#include "bw/math/matrix.hpp"
#include "game/actors/animation/character_reaction_policy.hpp"

namespace bw::game {
// This is the attack family named at_stagger in original content, not a
// stunned/damaged reaction. The admitted attack selector owns direction tests.
// Explicit slot708 remains available to a source-authored request; hardware
// request66c140 selects706/707. No hit or force result follows from this plan.
enum class CloseAttackVariant : std::uint8_t { ordinary, directed, alternate };
// Select after active-input/attack-edge and state admission. Processed movement
// is the source camera-local XYZ direction (homogeneous lane zero), not a
// target vector. Model forward is the original current comparison basis;
// preserve its magnitude. Finite input and representable arithmetic required.
// Below the source squared threshold selects ordinary; otherwise normalize,
// transform by camera basis, compare strict > cos(45 degrees). This does not
// authorize airborne attacks or hits. Both MCP phases I.1 and MEM.9 retain an
// explicit pure fixed-size consumer using shared vector/matrix operations.
CloseAttackVariant select_close_attack_variant(
    const math::Vector3<float>& processed_movement,
    const math::Matrix4<float>& camera_basis,
    const math::Vector3<float>& model_forward);
struct CloseAttackObservation {
  bool motion_completed{};
  bool cancellation_open{};
};
struct CloseAttackUpdate {
  ActionMotionPlan motion;
  bool active{};
  bool cancelled{};
  std::optional<std::uint32_t> exited_slot;
};
class CloseAttackPresentation {
 public:
  ActionMotionPlan begin(CloseAttackVariant variant);
  void queue_followup(CloseAttackVariant variant);
  // Apply this request before observing completion/CAN0 on the new owner.
  ActionMotionPlan take_followup(bool frame_crossed);
  CloseAttackUpdate advance(const CloseAttackObservation& observation);
  void interrupt() noexcept;

 private:
  std::optional<std::uint32_t> pending_;
  std::uint32_t slot_{};
  bool active_{};
};

struct AcceptedMagicPresentation {
  std::uint32_t slot{};         // Resolved by the owning, admitted spell state.
  bool complete_with_motion{};  // Source state flag1.
  bool attach_weapon{};         // Source flag2, scoped restoration on exit.
  bool face_camera{};           // Source flag4.
  bool special_actor_update{};  // Source state+0x5c.
  std::uint32_t effect_category{};
  // Required source-state registration; a motion slot alone cannot infer it.
  std::optional<bool> shooter_update;
};
struct MagicPresentationUpdate {
  ActionMotionPlan motion;
  bool active{};
  bool release_scope{};
};
class MagicPresentation {
 public:
  // Common613570 starts at FRAME1, not second1 or rate1. Source-specific
  // spells bypassing common entry need their own policy and must not use this.
  MagicPresentationUpdate begin(const AcceptedMagicPresentation& accepted);
  MagicPresentationUpdate advance(bool current_motion_completed);
  MagicPresentationUpdate interrupt() noexcept;
  const AcceptedMagicPresentation& scope() const noexcept { return scope_; }

 private:
  AcceptedMagicPresentation scope_{};
  bool active_{};
};
}  // namespace bw::game
