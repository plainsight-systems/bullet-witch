// Runner phase ordering and captured-velocity flight, independent of clocks.
// Core E.4 and MEM.9 apply in implementation: reject invalid observations
// before state mutation, use fixed plans/value storage with no frame
// allocations.
#include "scripted_jump_policy.hpp"

#include <cmath>
#include <stdexcept>

namespace bw::game {
namespace {
constexpr std::uint32_t kStartBase = 672, kFlightBase = 656, kLandingBase = 688,
                        kHardLanding = 704;
constexpr std::uint32_t kFamilyMask = 15, kRaisedBit = 8,
                        kRaisedLandingFamily = 9;
constexpr float kLandingVelocityScale = -.3F, kFeedbackRate = 4;
constexpr double kFirstFrame = 1;
constexpr float kTrapezoidWeight = .5F;
constexpr float kDirectionUnitTolerance = .001F;
ActionMotionPlan select(std::uint32_t slot, double frame, double blend) {
  ActionMotionPlan result;
  result.count = 1;
  result.requests[0] = {.slot = slot,
                        .start = ActionMotionStart::frames,
                        .start_frames = frame,
                        .blend_seconds = blend,
                        .fresh = true};
  return result;
}
void validate_step(float seconds, float gravity) {
  if (!std::isfinite(seconds) || seconds <= 0 || !std::isfinite(gravity) ||
      gravity <= 0) {
    throw std::invalid_argument("Invalid scripted-flight step");
  }
}
}  // namespace
ScriptedJumpTransition ScriptedJumpPolicy::begin(
    const AcceptedScriptedJump& request) {
  if (!math::all_finite(request.captured_velocity)) {
    throw std::invalid_argument("Nonfinite captured jump velocity");
  }
  if (!math::all_finite(request.captured_camera_direction) ||
      std::abs(math::dot(request.captured_camera_direction,
                         request.captured_camera_direction) -
               1.F) > kDirectionUnitTolerance) {
    throw std::invalid_argument(
        "Captured jump facing must be a finite unit direction");
  }
  if (phase_ != ScriptedJumpPhase::inactive) {
    throw std::logic_error("Scripted jump already active");
  }
  motion_word_ = request.motion_word;
  captured_velocity_ = request.captured_velocity;
  phase_ = ScriptedJumpPhase::start;
  return {.motion =
              select((motion_word_ & kFamilyMask) | kStartBase, kFirstFrame, 0),
          .phase = phase_};
}
ScriptedJumpTransition ScriptedJumpPolicy::begin_flight(
    const AcceptedScriptedJump& request) {
  static_cast<void>(begin(request));
  return enter_flight(false);
}
ScriptedJumpTransition ScriptedJumpPolicy::enter_flight(bool zero_vertical) {
  if (zero_vertical) captured_velocity_[1] = 0;
  phase_ = ScriptedJumpPhase::flight;
  return {.motion = select((motion_word_ & kFamilyMask) | kFlightBase, 0,
                           kSourceActionBlendSeconds),
          .phase = phase_,
          .flight_velocity = captured_velocity_};
}
ScriptedJumpTransition ScriptedJumpPolicy::advance_start(bool current_completed,
                                                         bool support_lost) {
  if (phase_ != ScriptedJumpPhase::start) {
    throw std::logic_error("Jump-start observation outside start phase");
  }
  if (current_completed) return enter_flight(false);
  if (support_lost) return enter_flight(true);
  return {.phase = phase_};
}
ScriptedJumpTransition ScriptedJumpPolicy::landed(float impact_velocity,
                                                  float gravity) {
  if (!std::isfinite(impact_velocity) || !std::isfinite(gravity) ||
      gravity <= 0) {
    throw std::invalid_argument("Invalid scripted landing observation");
  }
  if (phase_ != ScriptedJumpPhase::flight) {
    throw std::logic_error("Landing outside scripted flight");
  }
  if (!(impact_velocity < gravity * kLandingVelocityScale)) {
    phase_ = ScriptedJumpPhase::inactive;
    return {};
  }
  phase_ = ScriptedJumpPhase::landing;
  if (impact_velocity < -gravity) {
    landing_slot_ = kHardLanding;
    return {.motion = select(landing_slot_, 0, kSourceActionBlendSeconds),
            .phase = phase_};
  }
  // Raised jump families collapse to the camera-aligned forward landing.
  const bool align =
      motion_word_ < kHardLanding && (motion_word_ & kRaisedBit) != 0;
  landing_slot_ =
      ((align ? kRaisedLandingFamily : motion_word_) & kFamilyMask) |
      kLandingBase;
  return {.motion = select(landing_slot_, kFirstFrame, 0),
          .phase = phase_,
          .align_with_camera = align};
}
ScriptedJumpTransition ScriptedJumpPolicy::advance_landing(
    bool current_completed, bool cancellation_open, bool support_lost) {
  if (phase_ != ScriptedJumpPhase::landing) {
    throw std::logic_error("Landing observation outside landing phase");
  }
  if (current_completed || cancellation_open) {
    phase_ = ScriptedJumpPhase::inactive;
    return {.cancelled_slot = !current_completed && cancellation_open
                                  ? std::optional(landing_slot_)
                                  : std::nullopt};
  }
  if (support_lost) {
    // 5ff410 creates a fresh default StateJump; it does not copy landing data.
    motion_word_ = 0;
    captured_velocity_ = {};
    return enter_flight(false);
  }
  return {.phase = phase_};
}
ScriptedFlightStep scripted_flight_step(math::Vector3<float> velocity,
                                        float seconds, float gravity) {
  validate_step(seconds, gravity);
  if (!math::all_finite(velocity)) {
    throw std::invalid_argument("Nonfinite scripted-flight velocity");
  }
  const auto old_vertical = velocity[1];
  velocity[1] -= gravity * seconds;
  auto displacement = math::scale(velocity, seconds);
  displacement[1] = (old_vertical + velocity[1]) * seconds * kTrapezoidWeight;
  if (!math::all_finite(displacement) || !math::all_finite(velocity)) {
    throw std::invalid_argument("Scripted-flight arithmetic overflow");
  }
  return {displacement, velocity};
}
math::Vector3<float> scripted_flight_feedback(
    math::Vector3<float> velocity, math::Vector3<float> resolved_displacement,
    float seconds) {
  validate_step(seconds, 1);
  if (!math::all_finite(velocity) || !math::all_finite(resolved_displacement)) {
    throw std::invalid_argument("Nonfinite scripted-flight correction");
  }
  const auto inverse_seconds = 1 / seconds;
  const auto factor = seconds * kFeedbackRate;
  for (const auto axis : {std::size_t{0}, std::size_t{2}}) {
    velocity.at(axis) +=
        (resolved_displacement.at(axis) * inverse_seconds - velocity.at(axis)) *
        factor;
  }
  if (!math::all_finite(velocity)) {
    throw std::invalid_argument("Scripted-flight correction overflow");
  }
  return velocity;
}
}  // namespace bw::game
