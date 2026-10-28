// Verify explicit NPC posture/rate and retained facing independently of player
// hardware thresholds. Synthetic observations exercise the public value policy;
// native selector parity is verified separately with ignored original evidence.
#include "game/actors/npc/locomotion_policy.hpp"

#include <cmath>
#include <limits>

#include "../../../../fixtures/camera_fixture.hpp"

void npc_locomotion_policy_tests() {
  using bw::game::ActionMotionStart;
  using bw::game::NpcLocomotionInput;
  using bw::game::select_npc_locomotion;
  using camera_fixture::require;
  NpcLocomotionInput input{
      .destination = {0, 0, 10}, .posture = 0x30, .playback_rate = .25F};
  auto result = select_npc_locomotion(input);
  require(result.motion.slot == 0x30 && result.motion.rate == .25F &&
              result.motion.start == ActionMotionStart::preserve_phase,
          "NPC rate does not choose a hardware walk/dash family");
  input.playback_rate = 2;
  require(select_npc_locomotion(input).motion.rate == 2,
          "Authored NPC rate is not clamped to stick magnitude");
  input.destination = input.position;
  result = select_npc_locomotion(input);
  require(result.motion.slot == 0x31 && result.motion.rate == 1 &&
              result.movement_direction == bw::math::Vector3<float>{},
          "Coincident target idles without nonfinite direction");
  input.destination = {10, 0, 0};
  input.turn_limit_radians = 1;
  input.world_delta_seconds = .1F;
  result = select_npc_locomotion(input);
  require(result.facing_request &&
              std::abs(result.retained_heading[0] - std::sin(.1F)) < 1e-6F &&
              std::abs(result.retained_heading[2] - std::cos(.1F)) < 1e-6F,
          "NPC heading changes by bounded angular speed and world interval");
  input.turn_limit_radians = 0;
  input.weapon_raised = true;
  require(select_npc_locomotion(input).motion.slot == 0x3b,
          "Raised NPC right movement uses actor-facing directional sector");
  input.destination = {-10, 0, 0};
  require(select_npc_locomotion(input).motion.slot == 0x3a,
          "Raised NPC left movement selects the opposite strafe");
  input.destination = {0, 0, -10};
  require(select_npc_locomotion(input).motion.slot == 0x38,
          "Raised NPC backward motion preserves facing independently");
  input.stopped = true;
  input.signed_turn_rate = -.5F;
  result = select_npc_locomotion(input);
  require(result.motion.slot == 0x3d && result.motion.rate == .5F,
          "Stopped raised NPC uses signed turn animation rate");
  input.playback_rate = std::numeric_limits<float>::quiet_NaN();
  bool rejected = false;
  try {
    (void)select_npc_locomotion(input);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Nonfinite source observations reject before publication");
}
