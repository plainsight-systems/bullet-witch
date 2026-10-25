// Original spell phase witnesses: playback cannot substitute for the retained
// world/camera owners. E.4 checks missing observations before state mutation;
// MEM.9 keeps the tested policy in fixed value storage.
#include "game/spell_presentation_policy.hpp"

#include <stdexcept>
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace
void spell_presentation_policy_tests() {
  using bw::game::ActionMotionStart;
  using bw::game::SpellPresentationKind;
  using bw::game::SpellPresentationOutcome;
  using bw::game::SpellPresentationPhase;
  using bw::game::SpellPresentationPolicy;
  for (const auto kind :
       {SpellPresentationKind::ancient_wall, SpellPresentationKind::will_power,
        SpellPresentationKind::sacrifice}) {
    require(!spell_presentation_entry(kind).scope.shooter_update &&
                spell_presentation_entry(kind).scope.force_model_visibility,
            "Source spell state replaces the timed Shooter callback");
  }
  require(spell_presentation_entry(SpellPresentationKind::thunder)
              .scope.shooter_update,
          "Thunder retains the timed Shooter callback");
  SpellPresentationPolicy policy;
  auto entry = policy.begin(SpellPresentationKind::ancient_wall);
  require(entry.motion.requests[0].slot == 788 &&
              entry.motion.requests[0].start == ActionMotionStart::frames &&
              entry.motion.requests[0].start_frames == 1 &&
              entry.scope.saved_camera,
          "Common spell entry uses frame1 and declares saved camera ownership");
  require(policy.advance({.current_motion_completed = true}).release_scope &&
              !policy.interrupt().release_scope,
          "Ordinary completion releases accepted scope once");
  entry = policy.begin(SpellPresentationKind::rose_bomb);
  require(entry.motion.requests[0].slot == 771 &&
              entry.motion.requests[0].start_frames == 0 &&
              !entry.scope.common_entry && !entry.scope.camera_facing &&
              !entry.scope.force_model_visibility,
          "Rose Bomb uses its separate frame0 entry rather than common facing");
  static_cast<void>(policy.interrupt());
  static_cast<void>(policy.begin(SpellPresentationKind::sacrifice));
  require(
      policy.advance({.current_motion_completed = true}).outcome ==
              SpellPresentationOutcome::observation_required &&
          policy.phase() == SpellPresentationPhase::casting,
      "Sacrifice cannot infer retained target validity from clip completion");
  require(policy.advance({.target_valid = false}).release_scope,
          "Invalidated Sacrifice target releases the state immediately");
  static_cast<void>(policy.begin(SpellPresentationKind::tornado));
  require(
      policy.advance({.current_motion_completed = true, .camera_owned = false})
              .outcome == SpellPresentationOutcome::unchanged,
      "Tornado ignores clip completion before camera handoff");
  require(policy.advance({.camera_owned = true}).outcome ==
                  SpellPresentationOutcome::phase_changed &&
              policy.phase() == SpellPresentationPhase::finishing,
          "Tornado camera handoff enters its distinct ending owner");
  require(!policy.advance({}).release_scope &&
              policy.advance({.current_motion_completed = true}).release_scope,
          "Tornado ending waits for the retained current animation");
  static_cast<void>(policy.begin(SpellPresentationKind::meteor));
  require(
      policy.advance({.current_motion_completed = true}).outcome ==
          SpellPresentationOutcome::observation_required,
      "Meteor never replaces world phase observations with clip completion");
  auto update = policy.advance({.cinematic_segment_finished = true});
  require(update.motion.requests[0].slot == 1 && update.perform_root_contact &&
              update.request_after_root_contact &&
              policy.phase() == SpellPresentationPhase::recovery,
          "Meteor first cinematic boundary selects standing at frame0");
  update = policy.advance({.cinematic_segment_finished = true});
  require(update.motion.count == 0 &&
              policy.phase() == SpellPresentationPhase::world_wait,
          "Meteor second boundary transfers to its retained world owner");
  require(!policy.advance({.meteor_world_finished = false}).release_scope &&
              policy.advance({.meteor_world_finished = true}).release_scope,
          "Meteor release follows its explicit terminal world predicate");
  entry = policy.begin(SpellPresentationKind::thunder);
  require(entry.motion.requests[0].slot == 784 &&
              entry.motion.requests[0].start_frames == 0 &&
              !entry.scope.common_entry && entry.scope.force_model_visibility,
          "Thunder has a separate frame0 aim entry");
  require(policy.advance({.seconds = 11, .camera_owned = false}).outcome ==
              SpellPresentationOutcome::unchanged,
          "Thunder aim timer advances only while its camera is owned");
  update = policy.advance(
      {.seconds = 10, .current_motion_completed = true, .camera_owned = true});
  require(update.motion.requests[0].slot == 785 &&
              policy.phase() == SpellPresentationPhase::aiming,
          "Thunder exact timeout zero still admits the looping middle motion");
  update = policy.advance({.seconds = .01F,
                           .current_motion_completed = true,
                           .camera_owned = true});
  require(update.motion.count == 0 &&
              policy.phase() == SpellPresentationPhase::finishing,
          "Strictly negative timeout wins over simultaneous motion completion");
  require(policy.advance({.current_motion_completed = true}).release_scope,
          "Thunder terminal owner completes the retained motion");
  static_cast<void>(policy.begin(SpellPresentationKind::thunder));
  require(policy.accept_thunder_target().motion.requests[0].slot == 786,
          "Only an accepted target selects the strike animation");
  require(policy.advance({.current_motion_completed = true}).outcome ==
              SpellPresentationOutcome::observation_required,
          "Thunder strike cannot finish from animation completion alone");
  for (int segment = 0; segment < 5; ++segment) {
    update = policy.advance({.cinematic_segment_finished = true});
    require(!update.release_scope, "Cinematic boundaries retain strike scope");
  }
  require(
      policy.phase() == SpellPresentationPhase::finishing &&
          policy.interrupt().release_scope && !policy.interrupt().release_scope,
      "Five original cinematic segments enter terminal cleanup exactly once");
}
