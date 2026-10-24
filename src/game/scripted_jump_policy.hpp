// Accepted Runner jump phases, separate from hardware Dodger input. The caller
// owns collision, clocks and pose publication; this fixed value policy owns
// captured request velocity and original start/flight/landing transitions.
// Start completion wins over support loss and ignores CAN0. Flight uses the
// captured velocity, not pose-root displacement; landing uses the shared actor
// root/contact path. Advance no clocks here. Preflight returned motions before
// committing a copied policy. Grounded admission and weapon/camera capability
// are caller responsibilities. No heap allocation or borrowed resources.
// Both MCP design: Core E.4 (#re-design-invariants) validates before mutation;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) fixes storage.
// Original equations and distinct source obligations: scripted-jump-policy.md.
#pragma once
#include "alicia_action_policy.hpp"

namespace bw::game {
enum class ScriptedJumpPhase : std::uint8_t {
  inactive,
  start,
  flight,
  landing
};
struct AcceptedScriptedJump {
  std::uint32_t motion_word{};
  math::Vector3<float> captured_velocity{};
  // Accepted desired facing expressed in the admission camera basis. The
  // caller retains it through phases; fresh support loss uses source +Z.
  math::Vector3<float> captured_camera_direction{0, 0, 1};
};
struct ScriptedJumpTransition {
  ActionMotionPlan motion;
  ScriptedJumpPhase phase{ScriptedJumpPhase::inactive};
  std::optional<math::Vector3<float>> flight_velocity;
  std::optional<std::uint32_t> cancelled_slot;
  bool align_with_camera{};
};
class ScriptedJumpPolicy {
 public:
  ScriptedJumpTransition begin(const AcceptedScriptedJump& request);
  // Direct StateJump admission, including default zero-velocity support loss.
  ScriptedJumpTransition begin_flight(const AcceptedScriptedJump& request);
  ScriptedJumpTransition advance_start(bool current_completed,
                                       bool support_lost);
  // Called only after the actual airborne contact resolver reports support.
  ScriptedJumpTransition landed(float impact_velocity, float gravity);
  ScriptedJumpTransition advance_landing(bool current_completed,
                                         bool cancellation_open,
                                         bool support_lost);
  void interrupt() noexcept { phase_ = ScriptedJumpPhase::inactive; }
  ScriptedJumpPhase phase() const noexcept { return phase_; }

 private:
  ScriptedJumpTransition enter_flight(bool zero_vertical);
  std::uint32_t motion_word_{}, landing_slot_{};
  math::Vector3<float> captured_velocity_{};
  ScriptedJumpPhase phase_{ScriptedJumpPhase::inactive};
};
struct ScriptedFlightStep {
  math::Vector3<float> displacement{}, velocity{};
};
// Source6644b0 trapezoidal Y integration, then X/Z collision feedback at4*dt.
// Positive finite dt and gravity; all finite inputs/results. These functions
// are game equations above common vector arithmetic, not collision queries.
ScriptedFlightStep scripted_flight_step(math::Vector3<float> velocity,
                                        float seconds, float gravity);
math::Vector3<float> scripted_flight_feedback(
    math::Vector3<float> velocity, math::Vector3<float> resolved_displacement,
    float seconds);
}  // namespace bw::game
