// Hardware melee selection applies both-phase I.1 (#ri-explicit) to the
// caller-owned admission/basis contract and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) to fixed shared-math computations.
// Source reaction timers and action lifetimes; the caller owns presentation
// capabilities and applies plans before continuing grounded actor updates.
// Both MCP implementation corpora: E.4 validates before state writes; I.4
// separates frame crossings from completions; MEM.9 keeps updates allocation
// free. Source-specific equations are mapped in alicia-action-transitions.md.
#include "alicia_reaction_policy.hpp"

#include <cmath>
#include <span>
#include <stdexcept>

namespace bw::game {
namespace {
constexpr std::uint32_t kSlotCount = 1108;
constexpr std::uint32_t kOrdinaryPositive = 749;
constexpr std::uint32_t kOrdinaryNonpositive = 750;
constexpr std::uint32_t kSustainedNonpositive = 918;
constexpr std::uint32_t kSustainedPositive = 919;
constexpr std::uint32_t kAlternatePositive = 739;
constexpr std::uint32_t kAlternateNonpositive = 740;
constexpr std::array<std::uint32_t, 3> kAttacks{706, 707, 708};
constexpr std::array<std::uint32_t, 6> kDeathNonpositive{731, 733, 735,
                                                         736, 737, 738};
constexpr std::array<std::uint32_t, 3> kDeathPositive{730, 732, 734};
constexpr float kMinimumDirectionSquared = 1.0000001111620804e-6F;
constexpr float kSustainRandomScale = 2.5F;
constexpr float kSustainBaseSeconds = .3F;
constexpr double kMagicStartFrame = 1;

void validate_sample(float sample) {
  if (!std::isfinite(sample) || sample < 0 || sample > 1) {
    throw std::invalid_argument("Reaction random sample outside [0,1]");
  }
}
void validate_seconds(float seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid reaction duration");
  }
}
ActionMotionPlan plan(std::uint32_t slot) {
  ActionMotionPlan result;
  result.count = 1;
  result.requests[0].slot = slot;
  result.requests[0].fresh = true;
  return result;
}
// Source owns the horizontal projection and threshold. Generic dot/divide
// remain in shared math; the model-forward fallback is intentionally unscaled.
math::Vector3<float> impact_direction(const ReactionDirection& input) {
  if (!math::all_finite(input.incoming) ||
      !math::all_finite(input.actor_forward) ||
      !math::all_finite(input.comparison_axis)) {
    throw std::invalid_argument("Nonfinite reaction direction");
  }
  const math::Vector3<float> horizontal{input.incoming[0], 0,
                                        input.incoming[2]};
  const auto squared = math::dot(horizontal, horizontal);
  if (!std::isfinite(squared)) {
    throw std::invalid_argument("Reaction direction magnitude overflow");
  }
  return squared < kMinimumDirectionSquared
             ? input.actor_forward
             : math::divide(horizontal, std::sqrt(squared));
}
float sustain_duration(float random_unit) {
  return random_unit * kSustainRandomScale + kSustainBaseSeconds;
}
std::uint32_t attack_slot(CloseAttackVariant variant) {
  const auto index = static_cast<std::size_t>(variant);
  if (index >= kAttacks.size()) {
    throw std::invalid_argument("Unknown close attack variant");
  }
  return kAttacks.at(index);
}
// Original weighted selection subtracts in table order and falls through to
// the final entry, including an all-zero publication or random endpoint1.
std::size_t death_choice(std::span<const float> weights, float sample) {
  float total = 0;
  for (const auto weight : weights) {
    if (!std::isfinite(weight) || weight < 0) {
      throw std::invalid_argument("Invalid authored death weight");
    }
    total += weight;
  }
  if (!std::isfinite(total)) {
    throw std::invalid_argument("Authored death weights overflow");
  }
  auto remaining = sample * total;
  for (std::size_t index = 0; index < weights.size(); ++index) {
    remaining -= weights[index];
    if (remaining < 0) return index;
  }
  return weights.size() - 1;
}
}  // namespace
// Hardware direction chooses a presentation variant only after caller
// admission. The original reciprocal-then-scale order and strict cone boundary
// are retained.
CloseAttackVariant select_close_attack_variant(
    const math::Vector3<float>& processed_movement,
    const math::Matrix4<float>& camera_basis,
    const math::Vector3<float>& model_forward) {
  if (!math::all_finite(processed_movement) ||
      !math::all_finite(model_forward)) {
    throw std::invalid_argument("Nonfinite close attack direction");
  }
  for (const auto& row : camera_basis) {
    if (!math::all_finite(row)) {
      throw std::invalid_argument("Nonfinite close attack camera basis");
    }
  }
  const auto squared = math::dot(processed_movement, processed_movement);
  if (!std::isfinite(squared)) {
    throw std::invalid_argument("Close attack direction magnitude overflow");
  }
  if (!(squared > 0) || squared < kMinimumDirectionSquared) {
    return CloseAttackVariant::ordinary;
  }
  const auto direction =
      math::scale(processed_movement, 1.0F / std::sqrt(squared));
  const auto world = math::transform_direction<float>(direction, camera_basis);
  const auto alignment = math::dot(model_forward, world);
  if (!std::isfinite(alignment)) {
    throw std::invalid_argument("Close attack direction transform overflow");
  }
  constexpr double kDirectedConeRadians = .7853981852531433;
  return alignment > static_cast<float>(std::cos(kDirectedConeRadians))
             ? CloseAttackVariant::directed
             : CloseAttackVariant::ordinary;
}

ActionMotionPlan DamageReaction::begin(const AcceptedDamageReaction& accepted) {
  constexpr std::array<std::array<std::uint32_t, 4>, 3> slots{
      {{749, 750, 752, 751}, {753, 754, 756, 755}, {757, 758, 760, 759}}};
  constexpr double angle = .7853981852531433;
  const auto family = static_cast<std::size_t>(accepted.impact_class);
  if (family >= slots.size() || !math::all_finite(accepted.comparison_right)) {
    throw std::invalid_argument("Invalid accepted damage class/basis");
  }
  auto input = accepted.direction;
  input.actor_forward = {0, 0, 1};
  const auto direction = impact_direction(input);
  const auto front = math::dot(direction, input.comparison_axis);
  const auto right = math::dot(direction, accepted.comparison_right);
  const auto threshold = static_cast<float>(std::cos(angle));
  const std::size_t index = front > threshold    ? 0
                            : front < -threshold ? 1
                            : right >= threshold ? 3
                                                 : 2;
  const auto selected = slots.at(family).at(index);
  auto motion = plan(selected);
  motion.requests[0].blend_seconds = 0;  // StateDamageReaction constructor+58.
  slot_ = selected;
  crouched_ = accepted.impact_class == DamageImpactClass::crouched;
  active_ = true;
  return motion;
}
DamageReactionUpdate DamageReaction::advance(
    const DamageCompletion& observation) {
  DamageReactionUpdate result;
  if (!active_) return result;
  if (observation.source_force_idle) {
    result.motion = plan(1);
    result.motion.requests[0].fresh = false;
    active_ = false;
  } else if (!observation.motion_repeating) {
    if (observation.motion_completed) {
      active_ = false;
      result.requires_recovery_state = observation.source_low_health_recovery;
      result.return_crouched = crouched_ && !result.requires_recovery_state;
    } else if (observation.cancellation_open && !crouched_) {
      active_ = false;
      result.cancelled_slot = slot_;
    }
  }
  result.active = active_;
  return result;
}

ReactionUpdate SustainedDamageReaction::snapshot(
    ActionMotionPlan motion) const {
  return {motion, direction_, active_};
}
ReactionUpdate SustainedDamageReaction::begin(
    const AcceptedSustainedDamageReaction& accepted) {
  validate_seconds(accepted.recovery_seconds);
  validate_sample(accepted.random_unit);
  auto direction = impact_direction(accepted.direction);
  const bool positive =
      math::dot(direction, accepted.direction.comparison_axis) > 0;
  if (!positive) direction = math::scale(direction, -1.F);
  direction_ = direction;
  slot_ = positive ? kSustainedPositive : kSustainedNonpositive;
  recovery_ = accepted.recovery_seconds;
  sustain_ = sustain_duration(accepted.random_unit);
  active_ = true;
  return snapshot(plan(slot_));
}
ReactionUpdate SustainedDamageReaction::impact(
    const AcceptedSustainedDamageReaction& accepted) {
  if (!active_) throw std::logic_error("Damage reaction is inactive");
  validate_seconds(accepted.recovery_seconds);
  auto direction = impact_direction(accepted.direction);
  const bool positive =
      math::dot(direction, accepted.direction.comparison_axis) > 0;
  if (!positive) direction = math::scale(direction, -1.F);
  const auto selected =
      sustain_ <= 0 ? (positive ? kOrdinaryPositive : kOrdinaryNonpositive)
                    : (positive ? kSustainedPositive : kSustainedNonpositive);
  const auto request = selected == slot_ ? ActionMotionPlan{} : plan(selected);
  direction_ = direction;
  slot_ = selected;
  recovery_ = accepted.recovery_seconds;
  return snapshot(request);
}
ReactionUpdate SustainedDamageReaction::advance(
    const SustainedDamageStep& step) {
  validate_seconds(step.seconds);
  validate_sample(step.random_unit);
  if (!active_) return snapshot();
  auto recovery = recovery_ - step.seconds;
  auto sustain = sustain_ - step.seconds;
  if (!std::isfinite(recovery) || !std::isfinite(sustain)) {
    throw std::invalid_argument("Reaction clock overflow");
  }
  ActionMotionPlan request;
  // A one-shot reaction crosses frame10 back into the sustained pair before
  // checking recovery expiry. The source deliberately reverses this mapping.
  if (slot_ != kSustainedPositive && slot_ != kSustainedNonpositive &&
      step.crossed_frame_ten) {
    slot_ = slot_ == kOrdinaryNonpositive ? kSustainedPositive
                                          : kSustainedNonpositive;
    request = plan(slot_);
    sustain = sustain_duration(step.random_unit);
  }
  recovery_ = recovery;
  sustain_ = sustain;
  active_ = recovery > 0;
  return snapshot(request);
}
void SustainedDamageReaction::interrupt() noexcept { active_ = false; }

ActionMotionPlan CloseAttackPresentation::begin(CloseAttackVariant variant) {
  const auto selected = attack_slot(variant);
  slot_ = selected;
  pending_.reset();
  active_ = true;
  return plan(slot_);
}
void CloseAttackPresentation::queue_followup(CloseAttackVariant variant) {
  const auto selected = attack_slot(variant);
  if (!active_) throw std::logic_error("Close attack is inactive");
  pending_ = selected;
}
ActionMotionPlan CloseAttackPresentation::take_followup(bool frame_crossed) {
  if (!active_ || !pending_ || !frame_crossed) return {};
  slot_ = *pending_;
  pending_.reset();
  return plan(slot_);
}
CloseAttackUpdate CloseAttackPresentation::advance(
    const CloseAttackObservation& observation) {
  CloseAttackUpdate result;
  if (!active_) return result;
  if (observation.motion_completed || observation.cancellation_open) {
    result.cancelled = !observation.motion_completed;
    if (result.cancelled) result.exited_slot = slot_;
    interrupt();
  }
  result.active = active_;
  return result;
}
void CloseAttackPresentation::interrupt() noexcept {
  active_ = false;
  pending_.reset();
}

ActionMotionPlan accepted_death(const AcceptedDeathPresentation& accepted) {
  validate_sample(accepted.random_unit);
  // This selector consumes the already accepted impact vector literally;
  // unlike sustained damage it neither projects nor normalizes that vector.
  if (!math::all_finite(accepted.direction.incoming) ||
      !math::all_finite(accepted.direction.comparison_axis)) {
    throw std::invalid_argument("Nonfinite death direction");
  }
  const bool positive = math::dot(accepted.direction.incoming,
                                  accepted.direction.comparison_axis) > 0;
  if (accepted.crouched_state) {
    return plan(positive ? kAlternatePositive : kAlternateNonpositive);
  }
  return positive ? plan(kDeathPositive.at(death_choice(
                        accepted.positive_weights, accepted.random_unit)))
                  : plan(kDeathNonpositive.at(death_choice(
                        accepted.nonpositive_weights, accepted.random_unit)));
}
bool death_requires_dynamic_handoff(
    const DeathMotionObservation& observation) noexcept {
  return observation.contact_lost ||
         (!observation.source_handoff_blocked &&
          (observation.motion_completed || observation.motion_repeating));
}

MagicPresentationUpdate MagicPresentation::begin(
    const AcceptedMagicPresentation& accepted) {
  if (accepted.slot >= kSlotCount || !accepted.shooter_update.has_value()) {
    throw std::invalid_argument(
        "Unknown admitted magic motion or Shooter update contract");
  }
  if (active_) {
    throw std::logic_error("Interrupt prior magic scope before replacement");
  }
  scope_ = accepted;
  active_ = true;
  auto request = plan(scope_.slot);
  request.requests[0].start = ActionMotionStart::frames;
  request.requests[0].start_frames = kMagicStartFrame;
  return {request, true, false};
}
MagicPresentationUpdate MagicPresentation::advance(
    bool current_motion_completed) {
  if (active_ && scope_.complete_with_motion && current_motion_completed) {
    return interrupt();
  }
  return {{}, active_, false};
}
MagicPresentationUpdate MagicPresentation::interrupt() noexcept {
  const bool release = active_;
  active_ = false;
  return {{}, false, release};
}
}  // namespace bw::game
