// Source phase policy above prepared animation and externally owned spell
// capabilities. No scene-effect success follows from a motion completion.
// E.4 validates/requests observations before mutation; MEM.9 fixed storage.
#include "spell_presentation_policy.hpp"

#include <cmath>
#include <stdexcept>
namespace bw::game {
namespace {
constexpr std::uint32_t kStanding = 1;
constexpr std::uint32_t kAncientWall = 788, kRaven = 783, kRoseBomb = 771,
                        kWillPower = 789, kSacrifice = 787, kTornado = 779,
                        kMeteor = 782, kThunderStart = 784,
                        kThunderMiddle = 785, kThunderEnd = 786;
constexpr std::uint32_t kRavenEffect = 5, kTornadoEffect = 14;
constexpr float kThunderAimSeconds = 10;
constexpr std::uint8_t kThunderStrikeSegments = 5;
ActionMotionPlan select(std::uint32_t slot, double frame) {
  ActionMotionPlan result;
  result.count = 1;
  result.requests[0] = {.slot = slot,
                        .start = ActionMotionStart::frames,
                        .start_frames = frame,
                        .fresh = true};
  return result;
}
SpellPresentationUpdate needs_observation() {
  return {.outcome = SpellPresentationOutcome::observation_required,
          .perform_root_contact = false};
}
}  // namespace
SpellPresentationEntry spell_presentation_entry(SpellPresentationKind kind) {
  SpellPresentationEntry result;
  result.scope.common_entry = true;
  result.scope.camera_facing = true;
  result.scope.special_actor_update = true;
  std::uint32_t slot{};
  double first_frame = 1;
  switch (kind) {
    case SpellPresentationKind::ancient_wall:
      result.scope.shooter_update = false;
      slot = kAncientWall;
      result.scope.saved_camera = true;
      result.scope.retained_target = true;
      result.scope.common_effect_category = 1;
      break;
    case SpellPresentationKind::raven_panic:
      slot = kRaven;
      result.scope.retained_target = true;
      result.scope.cinematic = true;
      result.scope.common_effect_category = kRavenEffect;
      break;
    case SpellPresentationKind::rose_bomb:
      slot = kRoseBomb;
      first_frame = 0;
      result.scope.common_entry = false;
      result.scope.camera_facing = false;
      result.scope.retained_target = true;
      break;
    case SpellPresentationKind::will_power:
      result.scope.shooter_update = false;
      slot = kWillPower;
      result.scope.retained_target = true;
      result.scope.common_effect_category = 3;
      break;
    case SpellPresentationKind::sacrifice:
      result.scope.shooter_update = false;
      slot = kSacrifice;
      result.scope.saved_camera = true;
      result.scope.retained_target = true;
      result.scope.common_effect_category = 4;
      break;
    case SpellPresentationKind::tornado:
      result.scope.force_posture = true;
      slot = kTornado;
      result.scope.weapon_attachment = true;
      result.scope.cinematic = true;
      result.scope.retained_target = true;
      result.scope.common_effect_category = kTornadoEffect;
      break;
    case SpellPresentationKind::meteor:
      slot = kMeteor;
      result.scope.saved_camera = true;
      result.scope.cinematic = true;
      result.scope.retained_target = true;
      result.scope.common_effect_category = 0;
      break;
    case SpellPresentationKind::thunder:
      result.scope.force_posture = true;
      slot = kThunderStart;
      first_frame = 0;
      result.scope.common_entry = false;
      result.scope.camera_facing = false;
      result.scope.special_actor_update = false;
      result.scope.cinematic = true;
      result.scope.retained_target = true;
      break;
    default:
      throw std::invalid_argument("Unknown spell presentation kind");
  }
  result.motion = select(slot, first_frame);
  return result;
}
SpellPresentationEntry SpellPresentationPolicy::begin(
    SpellPresentationKind kind) {
  const auto entry = spell_presentation_entry(kind);
  if (phase_ != SpellPresentationPhase::inactive) {
    throw std::logic_error("Spell presentation already active");
  }
  kind_ = kind;
  phase_ = kind == SpellPresentationKind::thunder
               ? SpellPresentationPhase::aiming
               : SpellPresentationPhase::casting;
  aiming_seconds_ = kThunderAimSeconds;
  strike_segment_ = 0;
  return entry;
}
SpellPresentationUpdate SpellPresentationPolicy::interrupt() noexcept {
  const bool active = phase_ != SpellPresentationPhase::inactive;
  phase_ = SpellPresentationPhase::inactive;
  return {.outcome = active ? SpellPresentationOutcome::interrupted
                            : SpellPresentationOutcome::unchanged,
          .release_scope = active};
}
SpellPresentationUpdate SpellPresentationPolicy::accept_thunder_target() {
  if (kind_ != SpellPresentationKind::thunder ||
      phase_ != SpellPresentationPhase::aiming) {
    throw std::logic_error("Thunder target outside aiming state");
  }
  phase_ = SpellPresentationPhase::striking;
  strike_segment_ = 0;
  return {.motion = select(kThunderEnd, 0),
          .outcome = SpellPresentationOutcome::phase_changed};
}
SpellPresentationUpdate SpellPresentationPolicy::finish_thunder_aim() {
  if (kind_ != SpellPresentationKind::thunder ||
      phase_ != SpellPresentationPhase::aiming) {
    throw std::logic_error("Thunder aim exit outside aiming state");
  }
  phase_ = SpellPresentationPhase::finishing;
  return {.outcome = SpellPresentationOutcome::phase_changed};
}
SpellPresentationUpdate SpellPresentationPolicy::advance(
    const SpellPresentationObservation& observed) {
  if (!std::isfinite(observed.seconds) || observed.seconds < 0) {
    throw std::invalid_argument("Invalid spell observation timestep");
  }
  if (phase_ == SpellPresentationPhase::inactive) {
    return {.perform_root_contact = false};
  }
  bool complete = false;
  if (kind_ == SpellPresentationKind::thunder) {
    if (phase_ == SpellPresentationPhase::aiming) {
      if (!observed.camera_owned) return needs_observation();
      if (*observed.camera_owned) {
        aiming_seconds_ -= observed.seconds;
        if (aiming_seconds_ < 0) {
          auto result = finish_thunder_aim();
          result.perform_root_contact = false;
          return result;
        }
      }
      if (observed.current_motion_completed) {
        return {.motion = select(kThunderMiddle, 0),
                .outcome = SpellPresentationOutcome::phase_changed};
      }
      return {};
    }
    if (phase_ == SpellPresentationPhase::striking) {
      if (!observed.cinematic_segment_finished) return needs_observation();
      if (*observed.cinematic_segment_finished) {
        ++strike_segment_;
        if (strike_segment_ == kThunderStrikeSegments) {
          phase_ = SpellPresentationPhase::finishing;
        }
        return {.outcome = SpellPresentationOutcome::phase_changed};
      }
      return {};
    }
    complete = observed.current_motion_completed;
  } else if (kind_ == SpellPresentationKind::meteor) {
    if (phase_ == SpellPresentationPhase::world_wait) {
      if (!observed.meteor_world_finished) return needs_observation();
      complete = *observed.meteor_world_finished;
    } else {
      if (!observed.cinematic_segment_finished) return needs_observation();
      if (!*observed.cinematic_segment_finished) return {};
      const bool was_casting = phase_ == SpellPresentationPhase::casting;
      phase_ = was_casting ? SpellPresentationPhase::recovery
                           : SpellPresentationPhase::world_wait;
      return {.motion = was_casting ? select(kStanding, 0) : ActionMotionPlan{},
              .outcome = SpellPresentationOutcome::phase_changed,
              .request_after_root_contact = was_casting};
    }
  } else if (kind_ == SpellPresentationKind::tornado) {
    if (phase_ == SpellPresentationPhase::casting) {
      if (!observed.camera_owned) return needs_observation();
      if (!*observed.camera_owned) return {};
      phase_ = SpellPresentationPhase::finishing;
      return {.outcome = SpellPresentationOutcome::phase_changed,
              .perform_root_contact = false};
    }
    complete = observed.current_motion_completed;
  } else if (kind_ == SpellPresentationKind::sacrifice) {
    if (!observed.target_valid) return needs_observation();
    complete = !*observed.target_valid || observed.current_motion_completed;
  } else {
    complete = observed.current_motion_completed;
  }
  if (!complete) return {};
  auto result = interrupt();
  result.outcome = SpellPresentationOutcome::completed;
  result.perform_root_contact = kind_ == SpellPresentationKind::meteor ||
                                (kind_ == SpellPresentationKind::sacrifice &&
                                 observed.target_valid.value_or(false));
  return result;
}
}  // namespace bw::game
