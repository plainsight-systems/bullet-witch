// Recovered timer, frame-crossing and source weighted-choice witnesses. No
// combat or file assets are simulated. Both MCP phases: I.4/E.4 exercise typed
// observations and rejection; MEM.9 keeps the policy/fixtures bounded values.
#include "game/alicia_reaction_policy.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "game/player_controller.hpp"

namespace {
using bw::game::accepted_death;
using bw::game::AcceptedDeathPresentation;
using bw::game::AcceptedMagicPresentation;
using bw::game::AcceptedSustainedDamageReaction;
using bw::game::CloseAttackPresentation;
using bw::game::CloseAttackVariant;
using bw::game::death_requires_dynamic_handoff;
using bw::game::MagicPresentation;
using bw::game::SustainedDamageReaction;
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void movement_preprocessing() {
  using bw::game::process_player_movement;
  require(process_player_movement({.1F, 0}).direction == std::array<float, 2>{},
          "Sub-deadzone stick noise remains idle for action admission");
  constexpr float boundary = .15F / (10.0F / 9.0F);
  require(
      process_player_movement({std::nextafter(boundary, 0.0F), 0}).direction ==
              std::array<float, 2>{} &&
          process_player_movement({boundary, 0}).direction[0] > 0,
      "Original radial threshold rejects below and admits equality");
  const auto diagonal = process_player_movement({1, -1}).direction;
  require(std::abs(std::hypot(diagonal[0], diagonal[1]) - 1) < 1e-6F &&
              diagonal[0] == -diagonal[1],
          "Diagonal clamp preserves direction at unit magnitude");
  const auto edge = process_player_movement({-.998F, -.057F});
  require(
      edge.magnitude == 1,
      "Saturated source magnitude remains exactly one after vector rounding");
  const auto select = [](const bw::game::ProcessedPlayerMovement& movement) {
    return bw::game::select_ground_motion(
        {.magnitude = movement.magnitude,
         .movement_world = {movement.direction[0], 0, movement.direction[1]}});
  };
  const auto full = select(edge);
  require(full.locomotion_slot == 0x10 && full.motion.rate == 1,
          "The legal analog edge admits full-speed dash instead of throwing");
  // Compose producer and consumer over the stick square, including diagonals.
  for (int x = -16; x <= 16; ++x) {
    for (int y = -16; y <= 16; ++y) {
      const auto movement = process_player_movement(
          {static_cast<float>(x) / 16, static_cast<float>(y) / 16});
      require(movement.magnitude >= 0 && movement.magnitude <= 1,
              "Every finite stick sample preserves the scalar admission range");
      (void)select(movement);
    }
  }
}
void simulation_subdivision() {
  using bw::game::player_simulation_step;
  const double float_host_tick = static_cast<double>(1.0F / 60.0F);
  require(player_simulation_step(float_host_tick) == float_host_tick,
          "One float host tick remains one complete simulation interval");
  constexpr std::array elapsed{1.0 / 60, 1.0 / 30, .0168, .031, .05, .1, .25};
  for (const auto total : elapsed) {
    double remaining = total, consumed = 0;
    unsigned steps = 0;
    while (remaining > 0) {
      const auto next = player_simulation_step(remaining);
      require(
          next > 0 && next <= float_host_tick &&
              (total < float_host_tick || next >= float_host_tick / 2),
          "Subdivision creates bounded meaningful intervals, not a tiny tail");
      consumed += next;
      remaining -= next;
      require(++steps <= 16,
              "Maximum admitted elapsed has bounded subdivisions");
    }
    require(std::abs(consumed - total) <= 1e-15,
            "Balanced subdivision consumes the entire admitted elapsed time");
  }
  require(player_simulation_step(0) == 0, "Zero elapsed creates no simulation");
}
void ordinary_damage() {
  bw::game::DamageReaction policy;
  bw::game::AcceptedDamageReaction accepted;
  accepted.direction.incoming = {0, 8, 2};
  auto entry = policy.begin(accepted);
  require(entry.requests[0].slot == 749 && entry.requests[0].blend_seconds == 0,
          "Ordinary accepted forward damage uses actual PW0 slot and zero "
          "entry blend");
  auto update = policy.advance({false, false, true, false, false});
  require(!update.active && update.cancelled_slot == 749,
          "Standing damage exits on CAN0 with current slot handoff");
  accepted.impact_class = bw::game::DamageImpactClass::crouched;
  accepted.direction.incoming = {1, 0, 0};
  entry = policy.begin(accepted);
  require(entry.requests[0].slot == 759,
          "Crouched right impact preserves source table ordering");
  require(policy.advance({false, false, true, false, false}).active,
          "Crouched damage ignores CAN0 until source completion");
  update = policy.advance({true, false, false, false, false});
  require(!update.active && update.return_crouched,
          "Crouched completion requests original squat state");
  accepted.impact_class = bw::game::DamageImpactClass::heavy;
  accepted.direction.incoming = {};
  accepted.direction.actor_forward = {0, 0, -1};
  entry = policy.begin(accepted);
  require(entry.requests[0].slot == 753,
          "Ordinary degenerate direction uses world +Z, not actor forward");
  update = policy.advance({true, false, false, false, true});
  require(!update.active && update.requires_recovery_state,
          "Accepted low-health completion requires its real recovery owner");
  static_cast<void>(policy.begin(accepted));
  require(policy.advance({true, true, true, false, false}).active,
          "Repeating reaction does not use nonrepeat exit conditions");
}
void damage_lifetime() {
  SustainedDamageReaction policy;
  AcceptedSustainedDamageReaction accepted;
  accepted.direction.incoming = {0, 3, 2};
  accepted.recovery_seconds = 2;
  accepted.random_unit = 0;
  auto result = policy.begin(accepted);
  require(result.active && result.motion.requests[0].slot == 919 &&
              result.facing_target == bw::math::Vector3<float>{0, 0, 1},
          "Damage projects incoming Y and selects sustained positive side");
  result = policy.advance({.4F, false, 0});
  require(result.active && result.motion.count == 0,
          "Sustain expiry alone does not replace the motion");
  result = policy.impact(accepted);
  require(result.motion.requests[0].slot == 749,
          "Later accepted impact selects ordinary after sustain expires");
  require(policy.impact(accepted).motion.count == 0,
          "Identical impact preserves existing motion phase");
  result = policy.advance({0, true, .5F});
  require(result.motion.requests[0].slot == 918,
          "Source frame10 transition reverses ordinary-positive mapping");
  const auto saved = result.facing_target;
  bool rejected = false;
  try {
    static_cast<void>(policy.advance({-1, true, 0}));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && policy.active(), "Rejected update preserves state");
  result = policy.advance({2, false, 0});
  require(!result.active && result.facing_target == saved,
          "Recovery timer ends reaction independently of clip completion");
  accepted.direction.incoming = {};
  accepted.direction.actor_forward = {0, 0, -1};
  result = policy.begin(accepted);
  require(result.motion.requests[0].slot == 918 &&
              result.facing_target == bw::math::Vector3<float>{0, 0, 1},
          "Degenerate impact uses actor forward then nonpositive facing flip");
  policy.interrupt();
  require(!policy.advance({0, false, 0}).active,
          "Interrupted reaction does not resume on an update");
}
void close_attack_selection() {
  const bw::math::Matrix4<float> identity{
      {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
  const auto select = [&](const bw::math::Vector3<float>& movement,
                          const bw::math::Vector3<float>& facing) {
    return bw::game::select_close_attack_variant(movement, identity, facing);
  };
  require(
      select({0, 0, 0}, {0, 0, 1}) == CloseAttackVariant::ordinary &&
          select({0, 0, .0005F}, {0, 0, 1}) == CloseAttackVariant::ordinary &&
          select({0, 0, .001F}, {0, 0, 1}) == CloseAttackVariant::directed,
      "Source movement threshold separates stationary and directed strike");
  const auto threshold = static_cast<float>(std::cos(.7853981852531433));
  const auto over = std::nextafter(threshold, 1.0F);
  require(
      select({0, 0, 1}, {0, 0, threshold}) == CloseAttackVariant::ordinary &&
          select({0, 0, 1}, {0, 0, over}) == CloseAttackVariant::directed,
      "Exact source cone equality remains ordinary, strictly greater directs");
  auto camera = identity;
  camera[0] = {0, 0, -1, 0};
  camera[2] = {1, 0, 0, 0};
  require(
      bw::game::select_close_attack_variant({0, 0, 1}, camera, {1, 0, 0}) ==
              CloseAttackVariant::directed &&
          bw::game::select_close_attack_variant(
              {0, 0, -1}, camera, {1, 0, 0}) == CloseAttackVariant::ordinary,
      "Camera transform precedes comparison against current model facing");
  bool rejected = false;
  try {
    static_cast<void>(
        select({0, 0, std::numeric_limits<float>::infinity()}, {0, 0, 1}));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Nonfinite direction cannot authorize a variant");
}
void close_attack_lifetime() {
  CloseAttackPresentation policy;
  const auto entry = policy.begin(CloseAttackVariant::ordinary);
  require(
      entry.requests[0].slot == 706 && !entry.clear_upper,
      "Leaf melee policy leaves outgoing state cleanup to the action owner");
  policy.queue_followup(CloseAttackVariant::directed);
  const auto followup = policy.take_followup(true);
  require(followup.requests[0].slot == 707 && !followup.clear_upper &&
              policy.take_followup(true).count == 0,
          "Followup is consumed once before observing the new motion owner");
  auto result = policy.advance({false, true});
  require(!result.active && result.cancelled && result.exited_slot == 707,
          "CAN0 exit preserves current slot for return-state request");
  static_cast<void>(policy.begin(CloseAttackVariant::ordinary));
  result = policy.advance({true, true});
  require(!result.active && !result.cancelled && !result.exited_slot,
          "Completion takes priority over cancellation handoff");
}
void death_boundary() {
  AcceptedDeathPresentation accepted;
  accepted.direction.incoming = {0, 0, -1};
  accepted.nonpositive_weights = {1, 0, 1, 0, 0, 0};
  accepted.random_unit = .5F;
  require(accepted_death(accepted).requests[0].slot == 735,
          "Weight boundary subtracts to zero then chooses next positive bin");
  accepted.nonpositive_weights = {};
  require(accepted_death(accepted).requests[0].slot == 738,
          "Original all-zero pool falls through to last source entry");
  accepted.crouched_state = true;
  require(accepted_death(accepted).requests[0].slot == 740,
          "Current squat state selects nonpositive crouched death");
  accepted.direction.incoming = {0, 0, 1};
  require(accepted_death(accepted).requests[0].slot == 739,
          "Current squat state preserves opposite directional slot");
  require(death_requires_dynamic_handoff({false, true, false, false}) &&
              !death_requires_dynamic_handoff({true, true, true, false}) &&
              death_requires_dynamic_handoff({false, false, true, true}),
          "Death dynamic handoff distinguishes source block and support loss");
}
void magic_lifetime() {
  MagicPresentation policy;
  AcceptedMagicPresentation accepted;
  accepted.slot = 788;
  bool rejected = false;
  try {
    static_cast<void>(policy.begin(accepted));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Common magic requires its source Shooter update contract");
  accepted.shooter_update = false;
  accepted.complete_with_motion = true;
  accepted.attach_weapon = true;
  accepted.effect_category = 1;
  auto result = policy.begin(accepted);
  require(result.active &&
              result.motion.requests[0].start ==
                  bw::game::ActionMotionStart::frames &&
              result.motion.requests[0].start_frames == 1 &&
              result.motion.requests[0].fresh && policy.scope().attach_weapon,
          "Common magic starts at source frame1 and retains scope obligations");
  require(policy.advance(false).active, "Magic awaits source completion latch");
  result = policy.advance(true);
  require(!result.active && result.release_scope &&
              !policy.interrupt().release_scope,
          "Completion releases scope exactly once");
  accepted.complete_with_motion = false;
  static_cast<void>(policy.begin(accepted));
  require(policy.advance(true).active,
          "Owner-driven magic does not invent a universal clip-end exit");
  require(policy.interrupt().release_scope,
          "Explicit interruption returns same cleanup obligation");
}
}  // namespace
void alicia_reaction_policy_tests() {
  movement_preprocessing();
  simulation_subdivision();
  ordinary_damage();
  damage_lifetime();
  close_attack_selection();
  close_attack_lifetime();
  death_boundary();
  magic_lifetime();
}
