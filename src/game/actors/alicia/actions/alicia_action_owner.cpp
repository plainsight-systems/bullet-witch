// Grounded action entry synchronously delivers source effects to its borrowed
// owner (I.1/I.11/MEM.9, both implementation corpora read).
// Source-selected base ownership composes the prepared actor with bounded
// policies. Publication precedes policy commitment; actor clocks stay under
// the controller's sole tick ownership. Both MCP implementation corpora:
// I.11/E.4 preserve borrowing and rejection; MEM.9 keeps fixed value storage.
#include "alicia_action_owner.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "game/actors/alicia/player_actor.hpp"
#include "game/actors/alicia/player_controller.hpp"

namespace bw::game {
namespace {
constexpr float kDamageReturnFrame = 10;
constexpr std::size_t kSustainedNonpositiveSlot = 918;
constexpr std::size_t kSustainedPositiveSlot = 919;
AliciaActionUpdate unavailable() {
  return {.outcome = AliciaActionOutcome::unavailable_motion};
}
}  // namespace
bool AliciaActionOwner::available(const ActionMotionPlan& plan) const {
  for (const auto& request : plan.motions()) {
    if (!actor_->body_animations().motions().alias(request.slot).clip) {
      return false;
    }
  }
  return true;
}
bool AliciaActionOwner::forces_model_visibility() const noexcept {
  return kind_ == AliciaActionKind::magic &&
         (!spell_ || (spell_scope_ && spell_scope_->force_model_visibility));
}
bool AliciaActionOwner::updates_shooter() const noexcept {
  if (kind_ != AliciaActionKind::magic) return true;
  return spell_scope_ ? spell_scope_->shooter_update
                      : magic_.scope().shooter_update == true;
}
bool AliciaActionOwner::grounded_entry() const noexcept {
  return *phase_ == PlayerMovementPhase::grounded;
}
void AliciaActionOwner::publish_entry(ActionMotionPlan plan,
                                      std::optional<bool> force_before_body) {
  // Source order: outgoing ordinary upper exit, incoming effect, body request.
  // Continuations never repeat this outgoing state's retirement.
  if (kind_ == AliciaActionKind::none) publish(ordinary_shooter_exit());
  if (force_before_body) shooter_->force_shooter_posture(*force_before_body);
  publish(plan);
}
void AliciaActionOwner::remember_body() {
  const auto* state = actor_->animation_state(CharacterMotionBank::body);
  body_generation_ =
      state == nullptr ? std::nullopt : std::optional(state->generation);
}
void AliciaActionOwner::publish(const ActionMotionPlan& plan) {
  const auto result = actor_->apply_action_plan(plan);
  if (result.unavailable != 0) {
    throw std::logic_error("Preflighted action motion became unavailable");
  }
  remember_body();
}
AliciaActionUpdate AliciaActionOwner::begin_damage(
    const AcceptedDamageReaction& accepted) {
  if (!grounded_entry()) {
    return {.outcome = AliciaActionOutcome::unsupported_source_state};
  }
  DamageReaction candidate;
  const auto motion = candidate.begin(accepted);
  if (!available(motion)) return unavailable();
  publish_entry(motion, true);
  auto result = interrupt();
  damage_ = candidate;
  kind_ = AliciaActionKind::damage;
  remember_body();
  result.outcome = AliciaActionOutcome::started;
  return result;
}
AliciaActionUpdate AliciaActionOwner::begin_sustained_damage(
    const AcceptedSustainedDamageReaction& accepted) {
  if (!grounded_entry()) {
    return {.outcome = AliciaActionOutcome::unsupported_source_state};
  }
  SustainedDamageReaction candidate;
  const auto transition = candidate.begin(accepted);
  if (!available(transition.motion)) return unavailable();
  publish_entry(transition.motion);
  auto result = interrupt();
  sustained_ = candidate;
  kind_ = AliciaActionKind::sustained_damage;
  remember_body();
  result.outcome = AliciaActionOutcome::started;
  result.facing_target = transition.facing_target;
  return result;
}
AliciaActionUpdate AliciaActionOwner::sustained_impact(
    const AcceptedSustainedDamageReaction& accepted) {
  if (kind_ != AliciaActionKind::sustained_damage) {
    throw std::logic_error("Accepted impact needs active damage owner");
  }
  auto candidate = sustained_;
  const auto transition = candidate.impact(accepted);
  if (!available(transition.motion)) return unavailable();
  publish(transition.motion);
  sustained_ = candidate;
  return {.outcome = AliciaActionOutcome::unchanged,
          .facing_target = transition.facing_target};
}
AliciaActionUpdate AliciaActionOwner::begin_close_attack(
    CloseAttackVariant variant) {
  if (!grounded_entry()) {
    return {.outcome = AliciaActionOutcome::unsupported_source_state};
  }
  CloseAttackPresentation candidate;
  const auto motion = candidate.begin(variant);
  if (!available(motion)) return unavailable();
  publish_entry(motion, true);
  auto result = interrupt();
  close_attack_ = candidate;
  kind_ = AliciaActionKind::close_attack;
  remember_body();
  result.outcome = AliciaActionOutcome::started;
  return result;
}
AliciaActionUpdate AliciaActionOwner::queue_close_attack(
    CloseAttackVariant variant, float trigger_frame) {
  if (kind_ != AliciaActionKind::close_attack ||
      !std::isfinite(trigger_frame) || trigger_frame < 0) {
    throw std::invalid_argument("Invalid close-attack followup");
  }
  auto candidate = close_attack_;
  candidate.queue_followup(variant);
  auto preview = candidate;
  if (!available(preview.take_followup(true))) {
    return unavailable();
  }
  close_attack_ = candidate;
  followup_frame_ = trigger_frame;
  return {};
}
AliciaActionUpdate AliciaActionOwner::begin_magic(
    const AcceptedMagicPresentation& accepted) {
  if (!grounded_entry()) {
    return {.outcome = AliciaActionOutcome::unsupported_source_state};
  }
  MagicPresentation candidate;
  const auto transition = candidate.begin(accepted);
  if (!available(transition.motion)) return unavailable();
  publish_entry(transition.motion);
  auto result = interrupt();
  magic_ = candidate;
  kind_ = AliciaActionKind::magic;
  remember_body();
  result.outcome = AliciaActionOutcome::started;
  result.acquire_magic_scope = accepted;
  return result;
}
AliciaActionUpdate AliciaActionOwner::begin_spell(
    SpellPresentationKind accepted) {
  if (!grounded_entry()) {
    return {.outcome = AliciaActionOutcome::unsupported_source_state};
  }
  SpellPresentationPolicy candidate;
  const auto entry = candidate.begin(accepted);
  if (!available(entry.motion)) return unavailable();
  publish_entry(entry.motion);
  auto result = interrupt();
  spell_ = candidate;
  spell_scope_ = entry.scope;
  if (entry.scope.force_posture) shooter_->force_shooter_posture(false);
  kind_ = AliciaActionKind::magic;
  remember_body();
  result.outcome = AliciaActionOutcome::started;
  result.acquire_spell_scope = entry.scope;
  return result;
}
AliciaActionUpdate AliciaActionOwner::commit_spell_transition(
    SpellPresentationPolicy candidate, const SpellPresentationUpdate& update) {
  if (update.outcome == SpellPresentationOutcome::observation_required) {
    return {.outcome = AliciaActionOutcome::observation_required};
  }
  if (!available(update.motion)) return unavailable();
  if (update.request_after_root_contact) {
    post_contact_motion_ = update.motion;
  } else {
    publish(update.motion);
  }
  spell_ = candidate;
  AliciaActionUpdate result{.perform_root_contact =
                                update.perform_root_contact};
  if (update.release_scope) {
    const auto released_scope = spell_scope_;
    result = interrupt();
    result.release_spell_scope = released_scope;
    result.outcome = AliciaActionOutcome::completed;
    result.perform_root_contact = update.perform_root_contact;
  }
  return result;
}
AliciaActionUpdate AliciaActionOwner::accept_thunder_target() {
  if (!spell_) throw std::logic_error("Thunder target needs a spell owner");
  auto candidate = *spell_;
  const auto update = candidate.accept_thunder_target();
  return commit_spell_transition(candidate, update);
}
AliciaActionUpdate AliciaActionOwner::finish_thunder_aim() {
  if (!spell_) throw std::logic_error("Thunder aim exit needs a spell owner");
  auto candidate = *spell_;
  const auto update = candidate.finish_thunder_aim();
  return commit_spell_transition(candidate, update);
}
void AliciaActionOwner::finish_physical_step() {
  if (post_contact_motion_.count == 0) return;
  publish(post_contact_motion_);
  post_contact_motion_ = {};
}
AliciaActionUpdate AliciaActionOwner::begin_death(
    const AcceptedDeathPresentation& accepted) {
  if (!grounded_entry()) {
    return {.outcome = AliciaActionOutcome::unsupported_source_state};
  }
  const auto motion = accepted_death(accepted);
  if (!available(motion)) return unavailable();
  publish_entry(motion, true);
  auto result = interrupt();
  kind_ = AliciaActionKind::death;
  remember_body();
  result.outcome = AliciaActionOutcome::started;
  return result;
}
AliciaActionUpdate AliciaActionOwner::begin_melee_death(
    const AcceptedMeleeDeathPresentation& accepted) {
  const auto motion = accepted_melee_death(accepted);
  if (!available(motion)) return unavailable();
  publish_entry(motion, true);
  auto result = interrupt();
  kind_ = AliciaActionKind::death;
  remember_body();
  result.outcome = AliciaActionOutcome::started;
  return result;
}
AliciaActionUpdate AliciaActionOwner::interrupt() noexcept {
  AliciaActionUpdate result;
  if (kind_ != AliciaActionKind::none) {
    result.outcome = AliciaActionOutcome::interrupted;
  }
  if (spell_ && spell_->interrupt().release_scope) {
    result.release_spell_scope = spell_scope_;
  }
  spell_.reset();
  spell_scope_.reset();
  post_contact_motion_ = {};
  if (kind_ == AliciaActionKind::magic && magic_.interrupt().release_scope) {
    result.release_magic_scope = magic_.scope();
  }
  damage_.interrupt();
  sustained_.interrupt();
  close_attack_.interrupt();
  kind_ = AliciaActionKind::none;
  followup_frame_.reset();
  body_generation_.reset();
  dynamic_handoff_ = false;
  recovery_handoff_ = false;
  return result;
}
AliciaActionUpdate AliciaActionOwner::advance_before_motion(
    const AliciaActionStep& step) {
  if (!std::isfinite(step.seconds) || step.seconds < 0 ||
      step.seconds > std::numeric_limits<float>::max()) {
    throw std::invalid_argument("Invalid action-owner timestep");
  }
  if (post_contact_motion_.count != 0) {
    throw std::logic_error(
        "Pending action request requires physical completion");
  }
  if (kind_ == AliciaActionKind::none) return {};
  const auto* state = actor_->animation_state(CharacterMotionBank::body);
  if (state == nullptr || !body_generation_ ||
      state->generation != *body_generation_) {
    return {.outcome = AliciaActionOutcome::body_owner_changed};
  }
  if (kind_ == AliciaActionKind::damage) {
    if (recovery_handoff_) {
      return {.outcome = AliciaActionOutcome::recovery_state_required};
    }
    auto candidate = damage_;
    const auto transition =
        candidate.advance({state->completed, state->clip->repeating(),
                           actor_->motion_cancellable(), step.source_force_idle,
                           step.source_low_health_recovery});
    if (!available(transition.motion)) return unavailable();
    publish(transition.motion);
    damage_ = candidate;
    if (transition.requires_recovery_state) {
      recovery_handoff_ = true;
      return {.outcome = AliciaActionOutcome::recovery_state_required};
    }
    if (!transition.active) {
      auto result = interrupt();
      result.outcome = AliciaActionOutcome::completed;
      result.cancelled_slot = transition.cancelled_slot;
      result.return_crouched = transition.return_crouched;
      return result;
    }
    return {};
  }
  if (kind_ == AliciaActionKind::sustained_damage) {
    const bool crossing = animation::crossed_frame(*state, kDamageReturnFrame);
    const bool needs_random = crossing &&
                              state->binding != kSustainedNonpositiveSlot &&
                              state->binding != kSustainedPositiveSlot;
    if (needs_random && !step.random_unit) {
      return {.outcome = AliciaActionOutcome::random_required};
    }
    auto candidate = sustained_;
    const auto transition =
        candidate.advance({static_cast<float>(step.seconds), crossing,
                           step.random_unit.value_or(0)});
    if (!available(transition.motion)) return unavailable();
    publish(transition.motion);
    sustained_ = candidate;
    AliciaActionUpdate result{.facing_target = transition.facing_target};
    if (!transition.active) {
      result = interrupt();
      result.facing_target = transition.facing_target;
      result.outcome = AliciaActionOutcome::completed;
    }
    return result;
  }
  if (kind_ == AliciaActionKind::close_attack) {
    auto candidate = close_attack_;
    if (followup_frame_ && animation::crossed_frame(*state, *followup_frame_)) {
      const auto motion = candidate.take_followup(true);
      if (!available(motion)) return unavailable();
      publish(motion);
      followup_frame_.reset();
      state = actor_->animation_state(CharacterMotionBank::body);
    }
    const auto transition = candidate.advance(
        {state != nullptr && state->completed, actor_->motion_cancellable()});
    close_attack_ = candidate;
    if (!transition.active) {
      auto result = interrupt();
      result.outcome = AliciaActionOutcome::completed;
      result.cancelled_slot = transition.exited_slot;
      return result;
    }
    return {};
  }
  if (kind_ == AliciaActionKind::magic && spell_) {
    auto observation = step.spell;
    observation.seconds = static_cast<float>(step.seconds);
    observation.current_motion_completed = state->completed;
    auto candidate = *spell_;
    const auto transition = candidate.advance(observation);
    return commit_spell_transition(candidate, transition);
  }
  if (kind_ == AliciaActionKind::magic) {
    const auto transition = magic_.advance(state->completed);
    if (transition.release_scope) {
      auto result = interrupt();
      result.release_magic_scope = magic_.scope();
      result.outcome = AliciaActionOutcome::completed;
      return result;
    }
    return {};
  }
  dynamic_handoff_ =
      dynamic_handoff_ || death_requires_dynamic_handoff(
                              {state->completed, state->clip->repeating(),
                               step.death_handoff_blocked, step.support_lost});
  return {.outcome = dynamic_handoff_
                         ? AliciaActionOutcome::dynamic_body_required
                         : AliciaActionOutcome::unchanged};
}
}  // namespace bw::game
