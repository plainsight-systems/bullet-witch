// Original spell animation phases after external gameplay admission. This
// policy creates no target, projectile, camera task or world effect. Its entry
// contract declares those owned capabilities; callers must acquire them before
// publishing a copied policy's motion. Observations are source-owner
// predicates, not guessed clip-name transitions. Missing observations preserve
// state. Fixed value state, one game thread, no clocks/resources/allocations
// owned. The controller advances playback and fulfills scope release exactly
// once on completion/interruption. Pose/clock state survives paused appearance
// changes. Core E.4 (#re-design-invariants) and performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) consulted for design
// and implementation: explicit rejection/ownership and bounded steady-state
// work. Exact source, resource exclusions and required scope:
// spell-presentation.md.
#pragma once
#include "alicia_action_policy.hpp"

namespace bw::game {
enum class SpellPresentationKind : std::uint8_t {
  ancient_wall,
  raven_panic,
  rose_bomb,
  will_power,
  sacrifice,
  tornado,
  meteor,
  thunder
};
enum class SpellPresentationPhase : std::uint8_t {
  inactive,
  casting,
  aiming,
  striking,
  finishing,
  recovery,
  world_wait
};
struct SpellPresentationScope {
  // Entry-time camera facing; Rose Bomb has a separate live aim update.
  bool common_entry{}, camera_facing{}, special_actor_update{};
  bool weapon_attachment{}, saved_camera{}, retained_target{}, cinematic{};
  std::optional<std::uint32_t> common_effect_category;
  // Three source state registrations replace the entire timed Shooter update.
  bool shooter_update{true};
  // Thunder/Tornado reset selected targeting after their external camera scope.
  bool force_posture{};
};
struct SpellPresentationEntry {
  ActionMotionPlan motion;
  SpellPresentationScope scope;
};
// Source state constructors/entries, not a map from arbitrary asset names.
SpellPresentationEntry spell_presentation_entry(SpellPresentationKind kind);

enum class SpellPresentationOutcome : std::uint8_t {
  unchanged,
  phase_changed,
  completed,
  interrupted,
  observation_required
};
struct SpellPresentationUpdate {
  ActionMotionPlan motion;
  SpellPresentationOutcome outcome{SpellPresentationOutcome::unchanged};
  bool release_scope{};
  // Source callback dispatches root/contact for the previously evaluated model
  // interval. Meteor/Sacrifice can do so before a phase/completion transition;
  // an adapter must not reinterpret this as the newly selected clip's delta.
  bool perform_root_contact{true};
  bool request_after_root_contact{};
};
struct SpellPresentationObservation {
  float seconds{};
  bool current_motion_completed{};
  std::optional<bool> camera_owned;
  std::optional<bool> target_valid;
  // Original cinematic owner predicate4d9fa0==false, consumed once per step.
  std::optional<bool> cinematic_segment_finished;
  // Meteor terminal owner/camera predicate after its two cinematic phases.
  std::optional<bool> meteor_world_finished;
};
class SpellPresentationPolicy {
 public:
  SpellPresentationEntry begin(SpellPresentationKind kind);
  // Phase changes also require the original external owner handoff described
  // in spell-presentation.md. In particular Tornado's finishing phase restores
  // common camera/effect state and changes weapon attachment before completion.
  SpellPresentationUpdate advance(const SpellPresentationObservation& observed);
  // Separate accepted aim decisions from playback completion. Only a real
  // accepted target enters Thunder's strike; cancellation preserves the clip.
  SpellPresentationUpdate accept_thunder_target();
  SpellPresentationUpdate finish_thunder_aim();
  SpellPresentationUpdate interrupt() noexcept;
  SpellPresentationPhase phase() const noexcept { return phase_; }

 private:
  SpellPresentationKind kind_{};
  SpellPresentationPhase phase_{SpellPresentationPhase::inactive};
  float aiming_seconds_{};
  std::uint8_t strike_segment_{};
};
}  // namespace bw::game
