// Ownership extraction: design I.1/C.9 and implementation E.6 (#re-raii)
// retain stable actor/controller borrows. Both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps existing
// prepared buffers; update-only publication cannot replace draws, lights or
// scene view. Actor publication borrows ActorPublisher only; it cannot upload,
// bind or retire mission resources. Both-phase C.121 (#rh-abstract), I.11
// (#ri-raw), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) enforce
// the existing owner split without changing frame work or lifetimes. Complete
// balanced parent/model ticks; implementation E.4 preserves event and
// root-interval invariants, MEM.9 adds no steady-state allocations. Both
// corpora were read for design and implementation; see actor-model-clock-order.
// Contact mode follows the active source state: grounded root motion keeps
// floor support even with positive root Y; only airborne states apply ascent
// gating. Both MCP phases I.5 (#ri-pre) make that semantic input explicit;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps it a
// fixed per-query value without additional frame storage.
// Core F.21 (#rf-out-multi) returns the source capped magnitude alongside its
// rounded vector; I.1/E.4 keep that invariant at the producer instead of
// weakening action validation. MEM.9 retains fixed value-only work.
// Shared preprocessing preserves the original scale/deadzone/clamp order.
// Both-phase F.8 (#rf-pure) and MEM.9 keep this value-only operation
// independent of controller state, with no allocation for valid per-frame
// input. Direction stepping uses shared math with original inclusive
// collinearity thresholds and +Y fallback, preserving near-antipodal turn side.
// Both MCP phases: I.1/I.7 explicit direction policy and MEM.9 fixed simulation
// storage. Advance the original root-driven actor above its custom contact
// resolver. Header owns lifecycle and cost contracts; fixed state and prepared
// assets apply both-phase I.1/E.4/MEM.9. Numerical substeps bound contact
// displacement, following Unreal CharacterMovement's bounded simulation step
// design; original gravity/contact equations and authored clip displacement
// remain the inputs.
#include "player_controller.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "animation/aim_posture.hpp"
#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/direction.hpp"
#include "player_targeting.hpp"
namespace bw::game {
namespace {
constexpr float kGravity = 9.800000190734863F;
constexpr float kRadians = std::numbers::pi_v<float> / 180.0F;
constexpr float kMovementThreshold = 0.15F;
constexpr float kInputScale = 10.0F / 9.0F;
constexpr float kEvadeTurnRate = 8 * std::numbers::pi_v<float>;
constexpr math::DirectionTurnPolicy<float> kEvadeTurnPolicy{
    {0, 1, 0}, 0.9990000128746033F, 1.0000001111620804e-6F};
constexpr float kMotionBlend = 0.3F;
constexpr float kEvadeLandingBlend = 0.1F;
// Host/source elapsed is float; promote that same bound rather than comparing
// a float 60 Hz tick against a smaller double approximation.
constexpr double kMaximumStep = static_cast<double>(1.0F / 60.0F);
constexpr double kMaximumElapsed = 0.25;
constexpr double kBalancedStepFraction = 0.5;
constexpr std::uint32_t kPlayerExcludedFlags = 0x100;
constexpr float kPlayerGroundProbeExtension = 1;
animation::Matrix facing_rotation(float yaw) {
  formats::SkeletonTransform transform;
  transform.rotation = {0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
  transform.scale = {1, 1, 1};
  return animation::transform_matrix(transform);
}
}  // namespace
double player_simulation_step(double remaining_seconds) {
  if (!std::isfinite(remaining_seconds) || remaining_seconds < 0 ||
      remaining_seconds > kMaximumElapsed) {
    throw std::invalid_argument("Invalid remaining player simulation time");
  }
  // Pinned CharacterMovement GetSimulationTimeStep balances the final pair.
  // Our bounded caller consumes the exact remainder; no minimum-tick clamp
  // invents elapsed time and no tiny subtraction tail becomes a parent step.
  return remaining_seconds > kMaximumStep
             ? std::min(kMaximumStep, remaining_seconds * kBalancedStepFraction)
             : remaining_seconds;
}
ProcessedPlayerMovement process_player_movement(
    std::array<float, 2> direction) {
  if (!math::all_finite(direction)) {
    throw std::invalid_argument("Player movement axes must be finite");
  }
  direction = math::scale(direction, kInputScale);
  const float magnitude =
      std::sqrt(math::accumulated_dot(direction, direction));
  if (!std::isfinite(magnitude)) {
    throw std::invalid_argument("Player movement scale overflow");
  }
  if (magnitude < kMovementThreshold) return {};
  if (magnitude <= 1) return {direction, magnitude};
  // The original packet writes scalar one after reciprocal-vector scaling.
  // Recomputing its rounded vector length can exceed one and is not equivalent.
  return {math::scale(direction, 1.0F / magnitude), 1.0F};
}
PlayerBodySettings PlayerBodySettings::parse(
    const formats::CustomizationDefinition& tree) {
  constexpr std::array path{std::string_view("witch"),
                            std::string_view("actor"), std::string_view("pw0"),
                            std::string_view("body")};
  const resources::detail::Reader reader{tree.lookup(path)};
  constexpr std::uint64_t kHeight = 0x24, kAlternateHeight = 0x28,
                          kRadius = 0x2c, kSteeringMultiplier = 0x50,
                          kSteeringLimit = 0x54;
  PlayerBodySettings settings{
      std::bit_cast<float>(reader.u32(kRadius)),
      std::bit_cast<float>(reader.u32(kHeight)),
      std::bit_cast<float>(reader.u32(kAlternateHeight)),
      std::bit_cast<float>(reader.u32(kSteeringMultiplier)),
      std::bit_cast<float>(reader.u32(kSteeringLimit))};
  constexpr std::uint64_t kAimPitchLimits = 0x1c0;
  for (std::size_t index = 0; index < settings.aim_pitch_degrees.size();
       ++index) {
    settings.aim_pitch_degrees.at(index) = std::bit_cast<float>(
        reader.u32(kAimPitchLimits + index * sizeof(float)));
  }
  if (!std::isfinite(settings.radius) ||
      !std::isfinite(settings.standing_height) ||
      !std::isfinite(settings.alternate_height) ||
      !std::isfinite(settings.evade_steering_multiplier) ||
      !std::isfinite(settings.evade_steering_limit) ||
      !math::all_finite(settings.aim_pitch_degrees) ||
      settings.evade_steering_multiplier < 0 ||
      settings.evade_steering_limit < 0 || settings.radius <= 0 ||
      settings.standing_height < 2 * settings.radius ||
      settings.alternate_height < 2 * settings.radius) {
    resources::detail::malformed("Invalid authored player capsule dimensions");
  }
  return settings;
}
PlayerController::PlayerController(PlayerActor& actor,
                                   const scene::StaticCollision& collision,
                                   PlayerBodySettings settings,
                                   ScriptedPlayerPose placement,
                                   PlayerTargeting& targeting)
    : actor_(&actor),
      targeting_(&targeting),
      collision_(&collision),
      settings_(settings),
      state_{.pose = placement},
      action_(actor, state_.phase, *this) {
  if (!std::isfinite(settings.radius) ||
      !std::isfinite(settings.standing_height) ||
      !std::isfinite(settings.evade_steering_multiplier) ||
      !std::isfinite(settings.evade_steering_limit) ||
      !math::all_finite(settings.aim_pitch_degrees) ||
      settings.evade_steering_multiplier < 0 ||
      settings.evade_steering_limit < 0 || settings.radius <= 0 ||
      settings.standing_height < 2 * settings.radius ||
      !std::isfinite(placement.yaw_degrees) ||
      !std::ranges::all_of(
          placement.position,
          [](float value) { return std::isfinite(value); })) {
    throw std::invalid_argument("Invalid player body or initial placement");
  }
  state_.contact = collision_->move_character(
      {.previous_position = placement.position,
       .proposed_position = placement.position,
       .radius = settings.radius,
       .cylinder_height = settings.standing_height - 2 * settings.radius,
       .delta_seconds = static_cast<float>(kMaximumStep),
       .vertical_velocity = 0,
       .previously_grounded = true,
       .excluded_object_flags = kPlayerExcludedFlags,
       .ground_probe_extension = kPlayerGroundProbeExtension});
  state_.orientation = facing_rotation(placement.yaw_degrees * kRadians);
  state_.pose.position = state_.contact.position;
  if (!state_.contact.grounded) {
    enter_support_loss();
  }
}
void PlayerController::lower_review_weapon() noexcept {
  actor_->set_support_arm_permission(false);
  raised_ = {};
  state_.weapon_raised = false;
}
void PlayerController::restore_review_state(
    const PlayerMovementState& snapshot) noexcept {
  actor_->set_support_arm_permission(false);
  (void)action_.interrupt();
  scripted_jump_.interrupt();
  raised_ = {};
  idle_.reset();
  state_ = snapshot;
  state_.weapon_raised = false;
  state_.velocity = {};
  state_.phase = PlayerMovementPhase::grounded;
  evading_ = false;
  ballistic_ = false;
  had_exclusive_action_ = false;
  action_update_ = {};
  evade_steering_ = {};
}
bool PlayerController::script_motions_available(
    const ScriptedJumpPolicy& policy) const {
  const auto available = [&](const ActionMotionPlan& plan) {
    return std::ranges::all_of(plan.motions(), [&](const auto& request) {
      return actor_->body_animations()
          .motions()
          .alias(request.slot)
          .clip.has_value();
    });
  };
  auto flight = policy;
  if (!available(flight.advance_start(true, false).motion)) return false;
  auto hard_landing = flight;
  return available(flight.landed(-kGravity / 2, kGravity).motion) &&
         available(hard_landing.landed(-2 * kGravity, kGravity).motion);
}
AliciaActionUpdate PlayerController::begin_scripted_jump(
    const AcceptedScriptedJump& request) {
  if (state_.phase != PlayerMovementPhase::grounded ||
      !state_.contact.grounded ||
      scripted_jump_.phase() != ScriptedJumpPhase::inactive) {
    throw std::logic_error(
        "Accepted scripted jump requires grounded ownership");
  }
  ScriptedJumpPolicy candidate;
  const auto transition = candidate.begin(request);
  if (!script_motions_available(candidate) ||
      !actor_->body_animations()
           .motions()
           .alias(transition.motion.requests[0].slot)
           .clip) {
    return {.outcome = AliciaActionOutcome::unavailable_motion};
  }
  // Admission preserves the active body until every reachable phase exists.
  publish_scripted_transition(transition, state_.pose.yaw_degrees * kRadians);
  scripted_jump_ = candidate;
  scripted_camera_direction_ = request.captured_camera_direction;
  auto result = action_.interrupt();
  result.outcome = AliciaActionOutcome::started;
  state_.stance = AliciaStance::standing;
  idle_.reset();
  evading_ = false;
  ballistic_ = false;
  had_exclusive_action_ = false;
  return result;
}
void PlayerController::publish_scripted_transition(
    const ScriptedJumpTransition& transition, float camera_yaw_radians) {
  const auto result = actor_->apply_action_plan(transition.motion);
  if (result.unavailable != 0) {
    throw std::logic_error("Preflighted scripted phase became unavailable");
  }
  switch (transition.phase) {
    case ScriptedJumpPhase::inactive:
      state_.phase = PlayerMovementPhase::grounded;
      state_.velocity = {};
      break;
    case ScriptedJumpPhase::start:
      state_.phase = PlayerMovementPhase::takeoff;
      state_.velocity = {};
      break;
    case ScriptedJumpPhase::flight:
      state_.phase = PlayerMovementPhase::airborne;
      break;
    case ScriptedJumpPhase::landing:
      state_.phase = PlayerMovementPhase::landing;
      state_.velocity[1] = 0;
      break;
  }
  if (transition.flight_velocity) state_.velocity = *transition.flight_velocity;
  if (transition.align_with_camera) {
    state_.orientation = facing_rotation(camera_yaw_radians);
    state_.pose.yaw_degrees = camera_yaw_radians / kRadians;
  }
  if (transition.cancelled_slot) {
    action_update_.cancelled_slot = transition.cancelled_slot;
  }
}
void PlayerController::enter_support_loss() {
  ScriptedJumpPolicy fall;
  (void)fall.begin({});
  if (!script_motions_available(fall)) {
    throw std::logic_error("Default scripted fall phases are unavailable");
  }
  fall.interrupt();
  const auto transition = fall.begin_flight({});
  publish_scripted_transition(transition, state_.pose.yaw_degrees * kRadians);
  scripted_jump_ = fall;
  scripted_camera_direction_ = {0, 0, 1};
  idle_.reset();
  state_.stance = AliciaStance::standing;
  evading_ = false;
  ballistic_ = false;
}
bool PlayerController::accepts_shooter_input() const noexcept {
  if (action_.blocks_locomotion()) return false;
  const auto phase = scripted_jump_.phase();
  return phase == ScriptedJumpPhase::start ||
         phase == ScriptedJumpPhase::flight ||
         (phase == ScriptedJumpPhase::inactive &&
          state_.phase == PlayerMovementPhase::grounded);
}
bool PlayerController::publishes_posture_motion() const noexcept {
  if (action_.blocks_locomotion()) return false;
  const auto phase = scripted_jump_.phase();
  return phase == ScriptedJumpPhase::flight ||
         (phase == ScriptedJumpPhase::inactive &&
          state_.phase == PlayerMovementPhase::grounded);
}
void PlayerController::force_shooter_posture(bool ordinary_camera) noexcept {
  actor_->set_support_arm_permission(false);
  raised_ = {};
  state_.weapon_raised = false;
  targeting_->enter_forced_action(ordinary_camera);
}
bool PlayerController::update_raised(const PlayerAimObservation& aim,
                                     float seconds) {
  // Exclusive states use a timer-only posture receiver; three spell states
  // replace the timed update itself. Neither implies a new lower animation.
  if (!action_.updates_shooter()) return raised_.active();
  const auto* upper_before =
      actor_->animation_state(CharacterMotionBank::upper_body);
  const bool was_raised = raised_.active();
  const bool is_raised = raised_.advance(
      seconds, upper_before != nullptr,
      aim.accepted_refresh && accepts_shooter_input(), aim.weapon_class);
  state_.weapon_raised = is_raised;
  if (was_raised != is_raised && publishes_posture_motion()) {
    const auto* body = actor_->animation_state(CharacterMotionBank::body);
    if (body == nullptr) {
      throw std::logic_error("Raised state requires body motion");
    }
    (void)actor_->apply_action_plan(change_weapon_raised(
        state_.stance, static_cast<std::uint32_t>(body->binding), was_raised,
        is_raised));
  }
  return is_raised;
}
void PlayerController::update_aim_target(const PlayerAimObservation& aim) {
  // Action ownership vetoes the grip immediately; fading torso aim is not
  // permission to hold a weapon during melee, evade, spell or damage motion.
  actor_->set_support_arm_permission(raised_.active() &&
                                     accepts_shooter_input());
  // The source target uses the physical model before the render half-turn.
  actor_->set_aim_posture(
      raised_.active()
          ? aim_posture_target(
                {.anchor = aim.anchor,
                 .model = state_.orientation,
                 .retained_direction = aim.retained_direction,
                 .pitch_degrees = settings_.aim_pitch_degrees,
                 .crouched = state_.stance == AliciaStance::crouched})
          : math::Vector3<float>{0, 0, 1},
      raised_.active());
}
bool PlayerController::step_scripted(MovementStep step,
                                     const PlayerAimObservation& aim) {
  const auto [camera_yaw_radians, seconds] = step;
  const float dt = static_cast<float>(seconds);
  const auto phase_before = scripted_jump_.phase();
  if (phase_before == ScriptedJumpPhase::start) {
    publish_scripted_transition(
        scripted_jump_.advance_start(actor_->motion_finished(),
                                     !state_.contact.grounded),
        camera_yaw_radians);
  } else if (phase_before == ScriptedJumpPhase::landing) {
    publish_scripted_transition(
        scripted_jump_.advance_landing(actor_->motion_finished(),
                                       actor_->motion_cancellable(),
                                       !state_.contact.grounded),
        camera_yaw_radians);
  }
  const auto phase = scripted_jump_.phase();
  const bool raised = update_raised(aim, dt);
  if (phase == ScriptedJumpPhase::flight) {
    if (raised) {
      const math::Vector3<float> camera_forward{std::sin(camera_yaw_radians), 0,
                                                std::cos(camera_yaw_radians)};
      const auto camera_basis = math::orthonormal_basis(
          camera_forward, math::Vector3<float>{0, 1, 0});
      const auto facing = raised_facing_step(
          {.current = {state_.orientation[2][0], state_.orientation[2][1],
                       state_.orientation[2][2]},
           .requested = math::transform_direction<float>(
               scripted_camera_direction_, camera_basis),
           .camera_forward = camera_forward,
           .authored_reference = actor_->authored_facing_reference(),
           .seconds = dt,
           .posture_enabled = true});
      state_.pose.yaw_degrees = std::atan2(facing[0], facing[2]) / kRadians;
      state_.orientation = facing_rotation(state_.pose.yaw_degrees * kRadians);
    } else {
      scripted_camera_direction_ = {0, 0, 1};
    }
  }
  update_aim_target(aim);
  const auto* prior_body = actor_->animation_state(CharacterMotionBank::body);
  const bool suppress_root = prior_body != nullptr &&
                             prior_body->clip->repeating() &&
                             prior_body->completed;
  const auto root = actor_->pending_root_displacement();
  if (phase == ScriptedJumpPhase::inactive) return true;
  const auto previous = state_.pose.position;
  auto proposed = previous;
  if (phase == ScriptedJumpPhase::flight) {
    const auto flight = scripted_flight_step(state_.velocity, dt, kGravity);
    proposed = math::add(previous, flight.displacement);
    state_.velocity = flight.velocity;
  } else if (phase == ScriptedJumpPhase::landing && !suppress_root) {
    proposed = math::add(
        previous, math::transform_direction<float>(root, state_.orientation));
    state_.orientation = animation::multiply(state_.orientation,
                                             actor_->relative_root_rotation());
    state_.pose.yaw_degrees =
        std::atan2(state_.orientation[2][0], state_.orientation[2][2]) /
        kRadians;
  }
  // Starting retains contact without root displacement; only flight integrates
  // the accepted impulse. Landing returns to the original root/contact path.
  const bool flight = phase == ScriptedJumpPhase::flight;
  const auto contact = collision_->move_character(
      {.previous_position = previous,
       .proposed_position = proposed,
       .radius = settings_.radius,
       .cylinder_height = settings_.standing_height - 2 * settings_.radius,
       .delta_seconds = dt,
       .vertical_velocity = state_.velocity[1],
       .previously_grounded = !flight && state_.contact.grounded,
       .excluded_object_flags = kPlayerExcludedFlags,
       .ground_probe_extension = flight ? 0 : kPlayerGroundProbeExtension,
       .mode = flight ? scene::CharacterContactMode::airborne
                      : scene::CharacterContactMode::grounded});
  state_.pose.position = contact.position;
  state_.contact = contact;
  if (flight) {
    if (contact.grounded && state_.velocity[1] <= 0) {
      publish_scripted_transition(
          scripted_jump_.landed(state_.velocity[1], kGravity),
          camera_yaw_radians);
    } else {
      state_.velocity = scripted_flight_feedback(
          state_.velocity, math::subtract(contact.position, previous), dt);
    }
  }
  return true;
}
double PlayerController::advance(PlayerMovementInput input,
                                 float camera_yaw_radians, double delta_seconds,
                                 render::ActorPublisher& output,
                                 const PlayerActionObservations& observations) {
  if (!std::isfinite(delta_seconds) || delta_seconds < 0 ||
      delta_seconds > kMaximumElapsed || !std::isfinite(camera_yaw_radians) ||
      !std::ranges::all_of(input.direction, [](float axis) {
        return std::isfinite(axis) && std::abs(axis) <= 1;
      })) {
    throw std::invalid_argument(
        "Invalid player traversal input or elapsed time");
  }
  constexpr float kUnitTolerance = .001F;
  const auto& aim = observations.aim;
  const math::Vector3<float> anchor_forward{aim.anchor[2][0], aim.anchor[2][1],
                                            aim.anchor[2][2]};
  if (!math::all_finite(aim.anchor) ||
      !math::all_finite(aim.retained_direction) ||
      std::abs(math::dot(aim.retained_direction, aim.retained_direction) - 1) >
          kUnitTolerance ||
      std::abs(math::dot(anchor_forward, anchor_forward) - 1) >
          kUnitTolerance ||
      !std::isfinite(aim.signed_turn_input) ||
      !std::isfinite(aim.weapon_movement_scale) ||
      aim.weapon_movement_scale < 0 || aim.weapon_class > 3) {
    throw std::invalid_argument("Invalid accepted player aiming observation");
  }
  action_update_ = {};
  if (delta_seconds > 0 && input.crouch_pressed &&
      !action_.blocks_locomotion() &&
      scripted_jump_.phase() == ScriptedJumpPhase::inactive &&
      state_.phase == PlayerMovementPhase::grounded) {
    state_.stance = state_.stance == AliciaStance::standing
                        ? AliciaStance::crouched
                        : AliciaStance::standing;
    const auto* upper =
        actor_->animation_state(CharacterMotionBank::upper_body);
    if (upper != nullptr) {
      const auto change = adapt_upper_stance(
          state_.stance, static_cast<std::uint32_t>(upper->binding));
      if (change) {
        ActionMotionPlan plan;
        plan.requests[0] = *change;
        plan.count = 1;
        (void)actor_->apply_action_plan(plan);
      }
    }
  }
  double remaining = delta_seconds;
  bool completed_model = false;
  while (remaining > 0) {
    const double step_seconds = player_simulation_step(remaining);
    if (!step(camera_yaw_radians, input, step_seconds, observations)) break;
    action_.finish_physical_step();
    // Original hierarchy updates the actor parent before its model child.
    // Selection never erases the preceding raw movement interval.
    (void)actor_->advance_motion(step_seconds);
    actor_->complete_model_step(state_.pose, state_.orientation,
                                settings_.radius);
    completed_model = true;
    remaining -= step_seconds;
  }
  if (completed_model) {
    actor_->publish_completed_pose(output);
  } else {
    actor_->publish_pose(state_.pose, state_.orientation, settings_.radius,
                         output);
  }
  return delta_seconds - remaining;
}
bool PlayerController::step(float camera_yaw_radians, PlayerMovementInput input,
                            double seconds,
                            const PlayerActionObservations& observations) {
  if (scripted_jump_.phase() != ScriptedJumpPhase::inactive) {
    return step_scripted({camera_yaw_radians, seconds}, observations.aim);
  }
  const float dt = static_cast<float>(seconds);
  if (action_.blocks_locomotion() && !had_exclusive_action_) {
    // Replacing StateSquat runs its exit independently of the reaction's
    // eventual return-to-squat observation.
    state_.stance = AliciaStance::standing;
  }
  had_exclusive_action_ = action_.blocks_locomotion();
  const auto update = action_.advance_before_motion(
      {.seconds = seconds,
       .random_unit = observations.random_unit,
       .support_lost = !state_.contact.grounded,
       .death_handoff_blocked = observations.death_handoff_blocked,
       .source_force_idle = observations.source_force_idle,
       .source_low_health_recovery = observations.source_low_health_recovery,
       .spell = observations.spell});
  if (update.outcome != AliciaActionOutcome::unchanged ||
      update.release_spell_scope || update.perform_root_contact) {
    action_update_ = update;
  }
  switch (update.outcome) {
    case AliciaActionOutcome::random_required:
    case AliciaActionOutcome::observation_required:
    case AliciaActionOutcome::dynamic_body_required:
    case AliciaActionOutcome::recovery_state_required:
    case AliciaActionOutcome::body_owner_changed:
    case AliciaActionOutcome::unavailable_motion:
    case AliciaActionOutcome::unsupported_source_state:
      return false;
    default:
      break;
  }
  if (update.return_crouched) state_.stance = AliciaStance::crouched;
  auto aim = observations.aim;
  const bool is_raised = update_raised(aim, dt);
  const auto action_kind = action_.kind();
  const auto* previous_body =
      actor_->animation_state(CharacterMotionBank::body);
  const bool ordinary_action = action_kind == AliciaActionKind::damage ||
                               action_kind == AliciaActionKind::close_attack ||
                               action_kind == AliciaActionKind::magic;
  bool suppress_root =
      (!update.perform_root_contact.value_or(true)) ||
      (!update.perform_root_contact &&
       update.outcome == AliciaActionOutcome::completed) ||
      (ordinary_action && previous_body != nullptr &&
       previous_body->clip->repeating() && previous_body->completed);
  if (action_.blocks_locomotion()) idle_.reset();
  if (update.facing_target) {
    const auto forward = math::Vector3<float>{state_.orientation[2][0], 0,
                                              state_.orientation[2][2]};
    const auto facing = math::turn_toward(
        forward, *update.facing_target, kEvadeTurnRate * dt, kEvadeTurnPolicy);
    state_.pose.yaw_degrees = std::atan2(facing[0], facing[2]) / kRadians;
    state_.orientation = facing_rotation(state_.pose.yaw_degrees * kRadians);
  }
  const auto movement = process_player_movement(input.direction);
  input.direction = movement.direction;
  const float magnitude = movement.magnitude;
  const bool moving = magnitude != 0;
  const auto world_direction =
      std::array{std::cos(camera_yaw_radians) * input.direction[0] +
                     std::sin(camera_yaw_radians) * input.direction[1],
                 -std::sin(camera_yaw_radians) * input.direction[0] +
                     std::cos(camera_yaw_radians) * input.direction[1]};
  constexpr std::array starts{
      PlayerMotion::evade_forward_start, PlayerMotion::evade_backward_start,
      PlayerMotion::evade_left_start, PlayerMotion::evade_right_start};
  constexpr std::array middles{
      PlayerMotion::evade_forward_air, PlayerMotion::evade_backward_air,
      PlayerMotion::evade_left_air, PlayerMotion::evade_right_air};
  constexpr std::array endings{
      PlayerMotion::evade_forward_land, PlayerMotion::evade_backward_land,
      PlayerMotion::evade_left_land, PlayerMotion::evade_right_land};
  const bool cancelled_landing = state_.phase == PlayerMovementPhase::landing &&
                                 actor_->motion_cancellable();
  const bool completed_landing = state_.phase == PlayerMovementPhase::landing &&
                                 actor_->motion_finished() &&
                                 !cancelled_landing;
  if (cancelled_landing || completed_landing) {
    state_.phase = PlayerMovementPhase::grounded;
    evading_ = false;
    // Cancellation returns before root/contact. The next default-state update
    // selects locomotion; this frame still has the outgoing model owner.
    if (cancelled_landing) return true;
    actor_->select_motion(PlayerMotion::stand, 0);
    suppress_root = true;
  }
  const bool completed_takeoff =
      state_.phase == PlayerMovementPhase::takeoff && actor_->motion_finished();
  // A pending state change does not run the new state's update in this tick.
  // Completed start performs contact without root, then installs the middle
  // before the model child advances its first interval.
  suppress_root = suppress_root || completed_takeoff;
  const bool begin_ballistic = state_.phase == PlayerMovementPhase::airborne &&
                               evading_ && !ballistic_ &&
                               actor_->motion_finished();
  if (state_.phase == PlayerMovementPhase::grounded &&
      !action_.blocks_locomotion() &&
      update.outcome != AliciaActionOutcome::completed && !completed_landing) {
    if (input.evade_held && state_.contact.grounded) {
      // Replacing StateSquat sends its exit before DodgeStart entry.
      state_.stance = AliciaStance::standing;
      idle_.reset();
      evade_direction_ =
          std::abs(input.direction[0]) <= std::abs(input.direction[1])
              ? (input.direction[1] < 0 ? 1 : 0)
              : (input.direction[0] < 0 ? 2 : 3);
      constexpr std::array cardinal{0.0F, std::numbers::pi_v<float>,
                                    -std::numbers::pi_v<float> / 2,
                                    std::numbers::pi_v<float> / 2};
      evade_facing_ =
          camera_yaw_radians +
          (moving ? std::atan2(input.direction[0], input.direction[1]) : 0) -
          cardinal.at(evade_direction_);
      evading_ = true;
      ballistic_ = false;
      state_.phase = PlayerMovementPhase::takeoff;
      state_.velocity = {};
      // Outgoing ordinary-state upper exit precedes DodgeStart's body entry;
      // its forced posture/camera message follows that selection.
      (void)actor_->apply_action_plan(ordinary_shooter_exit());
      actor_->select_motion(starts.at(evade_direction_), 0);
      force_shooter_posture(true);
    } else {
      const auto* upper =
          actor_->animation_state(CharacterMotionBank::upper_body);
      const auto* body = actor_->animation_state(CharacterMotionBank::body);
      const auto selected = idle_.advance(
          {.stance = state_.stance,
           .magnitude = magnitude,
           .movement_world = {world_direction[0], 0, world_direction[1]},
           .camera_forward = {std::sin(camera_yaw_radians), 0,
                              std::cos(camera_yaw_radians)},
           .signed_turn_rate = observations.aim.signed_turn_input,
           .weapon_raised = is_raised,
           .weapon_movement_scale = observations.aim.weapon_movement_scale,
           .upper_slot =
               upper != nullptr
                   ? std::optional(static_cast<std::uint32_t>(upper->binding))
                   : std::nullopt,
           .upper_sampled_seconds = upper != nullptr ? upper->seconds : 0},
          body != nullptr
              ? std::optional(static_cast<std::uint32_t>(body->binding))
              : std::nullopt,
          actor_->motion_finished(), dt);
      ActionMotionPlan plan;
      plan.requests[0] = selected.ground.motion;
      plan.count = 1;
      (void)actor_->apply_action_plan(plan);
      if (selected.refresh_direction &&
          (moving || selected.ground.camera_facing)) {
        const math::Vector3<float> requested =
            moving
                ? math::normalize_or(math::Vector3<float>{world_direction[0], 0,
                                                          world_direction[1]},
                                     math::Vector3<float>{0, 0, 1})
                : math::Vector3<float>{std::sin(camera_yaw_radians), 0,
                                       std::cos(camera_yaw_radians)};
        auto facing = requested;
        if (is_raised) {
          facing = raised_facing_step(
              {.current = {state_.orientation[2][0], state_.orientation[2][1],
                           state_.orientation[2][2]},
               .requested = requested,
               .camera_forward = {std::sin(camera_yaw_radians), 0,
                                  std::cos(camera_yaw_radians)},
               .authored_reference = actor_->authored_facing_reference(),
               .seconds = dt,
               .posture_enabled = true});
        }
        state_.pose.yaw_degrees = std::atan2(facing[0], facing[2]) / kRadians;
        state_.orientation =
            facing_rotation(state_.pose.yaw_degrees * kRadians);
      }
    }
  }
  if (state_.phase == PlayerMovementPhase::takeoff && !completed_takeoff) {
    const float yaw = state_.pose.yaw_degrees * kRadians;
    const auto direction =
        math::turn_toward(math::Vector3<float>{std::sin(yaw), 0, std::cos(yaw)},
                          math::Vector3<float>{std::sin(evade_facing_), 0,
                                               std::cos(evade_facing_)},
                          kEvadeTurnRate * dt, kEvadeTurnPolicy);
    state_.pose.yaw_degrees = std::atan2(direction[0], direction[2]) / kRadians;
    state_.orientation = facing_rotation(state_.pose.yaw_degrees * kRadians);
  }
  if (state_.phase == PlayerMovementPhase::airborne && evading_) {
    for (std::size_t axis = 0; axis < evade_steering_.size(); ++axis) {
      evade_steering_.at(axis) = std::clamp(
          evade_steering_.at(axis) + world_direction.at(axis) * dt,
          -settings_.evade_steering_limit, settings_.evade_steering_limit);
    }
  }
  if (state_.phase != PlayerMovementPhase::grounded) {
    actor_->set_playback_rate(
        state_.phase == PlayerMovementPhase::landing && evading_ ? 2 : 1);
  }
  update_aim_target(observations.aim);
  const auto root = actor_->pending_root_displacement();
  if (update.perform_root_contact == false) return true;
  const auto previous = state_.pose.position;
  auto proposed = previous;
  const auto displacement = suppress_root ? math::Vector3<float>{}
                                          : math::transform_direction<float>(
                                                root, state_.orientation);
  if (!suppress_root) {
    state_.orientation = animation::multiply(state_.orientation,
                                             actor_->relative_root_rotation());
  }
  state_.pose.yaw_degrees =
      std::atan2(state_.orientation[2][0], state_.orientation[2][2]) / kRadians;
  // Root motion is consumed exactly once in world space. The skeletal pose
  // removes the consumed root transform; contacts correct this same body.
  for (std::size_t axis = 0; axis < proposed.size(); ++axis) {
    proposed.at(axis) += displacement.at(axis);
  }
  if (begin_ballistic) {
    // The middle state consumes its retained root before selecting fall.
    ballistic_ = true;
    actor_->select_motion(PlayerMotion::fall, 0);
  }
  if (state_.phase == PlayerMovementPhase::airborne && ballistic_) {
    const float old_vertical = state_.velocity[1];
    state_.velocity[1] -= kGravity * dt;
    proposed[1] += (old_vertical + state_.velocity[1]) * dt / 2;
    proposed[0] += (evading_ ? evade_steering_[0] : state_.velocity[0]) * dt;
    proposed[2] += (evading_ ? evade_steering_[1] : state_.velocity[2]) * dt;
  } else {
    const auto root_seconds = actor_->pending_root_seconds();
    for (std::size_t axis = 0; axis < state_.velocity.size(); ++axis) {
      state_.velocity.at(axis) =
          root_seconds > 0
              ? static_cast<float>(displacement.at(axis) / root_seconds)
              : 0;
    }
    if (state_.phase == PlayerMovementPhase::airborne && evading_) {
      proposed[0] +=
          evade_steering_[0] * settings_.evade_steering_multiplier * dt;
      proposed[2] +=
          evade_steering_[1] * settings_.evade_steering_multiplier * dt;
    }
  }
  const bool airborne = state_.phase == PlayerMovementPhase::airborne ||
                        state_.phase == PlayerMovementPhase::takeoff;
  const auto contact = collision_->move_character(
      {.previous_position = previous,
       .proposed_position = proposed,
       .radius = settings_.radius,
       .cylinder_height = (state_.stance == AliciaStance::crouched
                               ? settings_.alternate_height
                               : settings_.standing_height) -
                          2 * settings_.radius,
       .delta_seconds = dt,
       .vertical_velocity = state_.velocity[1],
       .previously_grounded = !airborne && state_.contact.grounded,
       .excluded_object_flags = kPlayerExcludedFlags,
       .ground_probe_extension = airborne ? 0 : kPlayerGroundProbeExtension,
       .mode = airborne ? scene::CharacterContactMode::airborne
                        : scene::CharacterContactMode::grounded});
  state_.pose.position = contact.position;
  state_.contact = contact;
  if (completed_takeoff) {
    state_.phase = PlayerMovementPhase::airborne;
    state_.velocity = {};
    evade_steering_ = {};
    actor_->select_motion(middles.at(evade_direction_), 0);
    return true;
  }
  if (state_.phase == PlayerMovementPhase::airborne) {
    if (contact.hit_ceiling && state_.velocity[1] > 0) state_.velocity[1] = 0;
    if (contact.grounded && state_.velocity[1] <= 0) {
      const float impact_velocity = state_.velocity[1];
      state_.velocity[1] = 0;
      if (!evading_ && impact_velocity >= -kGravity * kMotionBlend) {
        state_.phase = PlayerMovementPhase::grounded;
        actor_->select_motion(PlayerMotion::stand, kMotionBlend);
        return true;
      }
      state_.phase = PlayerMovementPhase::landing;
      actor_->select_motion(
          evading_ ? endings.at(evade_direction_) : PlayerMotion::hard_land,
          evading_ ? kEvadeLandingBlend : kMotionBlend);
      // Landing entry installs its rate before this frame's child-model tick.
      // Deferring this until the next parent step changes the source clock.
      actor_->set_playback_rate(evading_ ? 2 : 1);
    }
  } else if (!airborne && !contact.grounded) {
    if (action_.kind() == AliciaActionKind::death) return true;
    if (action_.blocks_locomotion()) action_update_ = action_.interrupt();
    enter_support_loss();
  }
  return true;
}
}  // namespace bw::game
