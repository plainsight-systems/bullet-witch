// Source-equation witnesses for grounded selection and independent body/weapon
// requests. No assets, combat simulation or native host are involved. Both MCP
// phases: I.1/E.4 test explicit inputs and rejection; MEM.9 keeps fixture plans
// bounded values. Source provenance: alicia-action-policy.md.
#include "game/actors/alicia/actions/alicia_action_policy.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
using bw::game::accepted_reload;
using bw::game::accepted_shot;
using bw::game::accepted_weapon_change;
using bw::game::ActionMotionBank;
using bw::game::ActionMotionStart;
using bw::game::adapt_upper_stance;
using bw::game::AliciaStance;
using bw::game::change_weapon_raised;
using bw::game::completed_weapon_motion;
using bw::game::GroundMotionInput;
using bw::game::select_ground_motion;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
bool close(float a, float b) { return std::abs(a - b) < .00001F; }
void ground_selection() {
  GroundMotionInput input;
  input.movement_world = {0, 0, 1};
  input.magnitude = .45F;
  auto selected = select_ground_motion(input);
  require(selected.motion.slot == 0 && close(selected.motion.rate, .5F) &&
              selected.motion.start == ActionMotionStart::preserve_phase,
          "Half-speed walk retains source phase and analog rate");
  input.magnitude = .9F;
  require(select_ground_motion(input).motion.slot == 0,
          "Dash threshold equality remains walking");
  input.magnitude = 1;
  selected = select_ground_motion(input);
  require(selected.motion.slot == 0x10 && selected.motion.rate == 1,
          "Full magnitude selects dash family");
  input.weapon_raised = true;
  input.weapon_movement_scale = .5F;
  selected = select_ground_motion(input);
  require(selected.motion.slot == 9 && close(selected.motion.rate, .5F / .9F) &&
              selected.camera_facing,
          "Raised weapon scaling precedes family selection");
  input.movement_world = {0, 0, -1};
  require(select_ground_motion(input).motion.slot == 8,
          "Raised backward family");
  input.movement_world = {-1, 0, 0};
  require(select_ground_motion(input).motion.slot == 10, "Raised left family");
  input.movement_world = {1, 0, 0};
  require(select_ground_motion(input).motion.slot == 11, "Raised right family");
  // The authored +15-degree offset makes a 50-degree direction forward,
  // despite crossing an unbiased camera's 45-degree boundary.
  constexpr float diagonal_x = .7660444431F;
  constexpr float diagonal_z = .6427876097F;
  input.movement_world = {diagonal_x, 0, diagonal_z};
  require(select_ground_motion(input).motion.slot == 9,
          "Directional selection includes the original camera bias");
  input.stance = AliciaStance::crouched;
  selected = select_ground_motion(input);
  require(selected.motion.slot == 0x99 && selected.motion.rate == .5F,
          "Crouched analog rate does not divide by standing threshold");
  input.magnitude = 0;
  input.signed_turn_rate = -.25F;
  selected = select_ground_motion(input);
  require(selected.motion.slot == 0x9d && selected.motion.rate == .25F,
          "Raised crouched idle turn selects signed direction");
  input.signed_turn_rate = 0;
  input.upper_slot = 0x169;
  input.upper_sampled_seconds = .75;
  selected = select_ground_motion(input);
  require(selected.locomotion_slot == 0x9c && selected.motion.slot == 0x16c &&
              selected.motion.start_seconds == .75 &&
              selected.motion.start == ActionMotionStart::seconds,
          "Idle mirrors the retained upper family at its observed phase");
  input.weapon_raised = false;
  input.upper_slot.reset();
  input.signed_turn_rate = 1;
  require(select_ground_motion(input).motion.slot == 0x91,
          "Unraised idle ignores shooting turn selection");
  input.magnitude = std::numeric_limits<float>::quiet_NaN();
  bool rejected = false;
  try {
    static_cast<void>(select_ground_motion(input));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Malformed movement cannot become an animation request");
}
void paired_receivers() {
  const auto raise =
      change_weapon_raised(AliciaStance::standing, 1, false, true);
  require(raise.clear_upper && raise.count == 2 &&
              raise.requests[0].slot == 0x251 &&
              raise.requests[0].bank == ActionMotionBank::upper_body &&
              raise.requests[0].channel == 1 && !raise.requests[0].fresh &&
              raise.requests[1].slot == 2 && raise.requests[1].fresh &&
              raise.weapon_rest_slot == 2,
          "Raise resets upper and independently selects desired weapon stance");
  const auto lower =
      change_weapon_raised(AliciaStance::crouched, 0x9c, true, false);
  require(lower.requests[0].slot == 0x28c && lower.requests[1].slot == 5 &&
              lower.weapon_rest_slot == 1 && !lower.clear_upper,
          "Crouched lowering keeps separate upper and weapon requests");
  require(
      change_weapon_raised(AliciaStance::standing, 1, true, true).count == 0,
      "Raised refresh does not restart raising");
  const auto reload = accepted_reload({AliciaStance::crouched, 0x9a, 2});
  require(reload.count == 2 &&
              reload.requests[0].bank == ActionMotionBank::weapon &&
              reload.requests[0].slot == 4 && reload.requests[0].fresh &&
              reload.requests[1].slot == 0x1aa && !reload.requests[1].fresh,
          "Reload preserves source order, class, stance and low direction");
  const auto shot = accepted_shot({AliciaStance::standing, 0x19, 2, true});
  require(bw::game::permits_support_hand(std::nullopt) &&
              bw::game::permits_support_hand(shot.requests[0].slot) &&
              !bw::game::permits_support_hand(reload.requests[1].slot) &&
              !bw::game::permits_support_hand(raise.requests[0].slot) &&
              !bw::game::permits_support_hand(lower.requests[0].slot) &&
              !bw::game::permits_support_hand(
                  accepted_weapon_change(AliciaStance::standing, 12)
                      .requests[0]
                      .slot) &&
              !bw::game::permits_support_hand(0xffff),
          "Grip releases for non-discharge upper owners, including unknown");
  require(shot.motions().size() == 3 && shot.requests[0].slot == 0x219 &&
              shot.requests[1].slot == 6 && shot.requests[2].slot == 0x119 &&
              shot.requests[2].bank == ActionMotionBank::additive &&
              shot.requests[2].channel == 0 && shot.requests[2].fresh,
          "Accepted shot independently submits discharge, weapon and recoil");
  require(accepted_shot({AliciaStance::standing, 0x2c0, 0, false})
                  .requests[2]
                  .slot == 0x2c1,
          "Camera-less actor recoil uses its specific native slot");
  require(
      accepted_weapon_change(AliciaStance::crouched, 0x9c).requests[0].slot ==
          0x14c,
      "Hardware switch retains crouched direction family");
  require(accepted_shot({AliciaStance::standing, 0x2c0, 0, true})
                  .requests[2]
                  .slot == 0xf0,
          "Airborne player retains camera-owned body-family recoil");
  const auto migrate = adapt_upper_stance(AliciaStance::crouched, 0x179);
  require(migrate && migrate->slot == 0x189 && migrate->fresh &&
              migrate->start == ActionMotionStart::preserve_phase &&
              !adapt_upper_stance(AliciaStance::crouched, 0x189) &&
              !adapt_upper_stance(AliciaStance::standing, 0x2c1),
          "Only mismatched paired upper families migrate, preserving phase");
  const auto rest = completed_weapon_motion(false, true, 2);
  require(
      !completed_weapon_motion(true, true, 2) &&
          !completed_weapon_motion(false, false, 2) && rest && rest->slot == 2,
      "Only completed nonrepeating weapon motion returns to desired stance");
}

void idle_lifecycle() {
  bw::game::GroundIdlePolicy idle;
  GroundMotionInput input;
  auto selected = idle.advance(input, 1, false, 60);
  require(selected.ground.motion.slot == 1 && !selected.special_idle,
          "Exactly sixty seconds remains ordinary idle");
  selected = idle.advance(input, 1, false, .25F);
  require(selected.ground.motion.slot == 1 && selected.refresh_direction,
          "Crossing the threshold arms stay without selecting it this tick");
  selected = idle.advance(input, 1, true, 0);
  require(selected.ground.motion.slot == 767 && selected.special_idle &&
              !selected.refresh_direction && !selected.ground.motion.fresh,
          "Next update starts stay, ignoring an unrelated completed clip");
  selected = idle.advance(input, 767, true, .25F);
  require(selected.ground.motion.slot == 1 && !selected.special_idle &&
              !selected.refresh_direction,
          "Stay completion restores idle but retains old-slot direction phase");
  idle.advance(input, 1, false, 61);
  input.upper_slot = 0x251;
  selected = idle.advance(input, 767, false, 0);
  require(selected.ground.motion.slot == 0x251 && !selected.special_idle,
          "Upper action interrupts armed stay before body selection");
  input.upper_slot.reset();
  idle.advance(input, 1, false, 61);
  input.magnitude = 1;
  input.movement_world = {0, 0, 1};
  require(!idle.advance(input, 767, false, 0).special_idle,
          "Movement interrupts stay immediately");
  input.magnitude = 0;
  idle.advance(input, 1, false, 61);
  input.stance = AliciaStance::crouched;
  require(!idle.advance(input, 767, false, 61).special_idle,
          "Standing idle timer does not run in crouch");
  input.stance = AliciaStance::standing;
  idle.advance(input, 1, false, 60);
  bool rejected = false;
  try {
    idle.advance(input, 1, false, -1);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && !idle.advance(input, 1, false, 0).special_idle,
          "Invalid delta rejects without corrupting threshold state");
  idle.advance(input, 1, false, 1);
  idle.reset();
  require(!idle.advance(input, 767, false, 0).special_idle,
          "Standing-state exit resets both armed variation and timer");
}
}  // namespace
void alicia_action_policy_tests() {
  ground_selection();
  paired_receivers();
  idle_lifecycle();
}
