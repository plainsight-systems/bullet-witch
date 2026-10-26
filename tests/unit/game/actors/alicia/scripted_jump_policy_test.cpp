// Runner source phase/velocity witnesses in process; no hardware or fabricated
// collision solver. E.4 tests rejected/ordered transitions; MEM.9 fixed values.
#include "game/actors/alicia/actions/scripted_jump_policy.hpp"

#include <cmath>
#include <stdexcept>
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}  // namespace
void scripted_jump_policy_tests() {
  using bw::game::ScriptedJumpPhase;
  bw::game::ScriptedJumpPolicy policy;
  bool invalid_direction = false;
  try {
    static_cast<void>(policy.begin({1, {}, {0, 0, 0}}));
  } catch (const std::invalid_argument&) {
    invalid_direction = true;
  }
  require(invalid_direction && policy.phase() == ScriptedJumpPhase::inactive,
          "Invalid captured facing rejects without changing jump state");
  auto state = policy.begin({1, {2, 4.2F, -3}, {1, 0, 0}});
  require(state.motion.requests[0].slot == 673 &&
              state.motion.requests[0].start_frames == 1 &&
              state.motion.requests[0].blend_seconds == 0,
          "Stationary jump starts at source frame1 with zero blend");
  require(policy.advance_start(false, false).phase == ScriptedJumpPhase::start,
          "Start remains grounded before current clip completion");
  state = policy.advance_start(true, true);
  require(state.motion.requests[0].slot == 657 && state.flight_velocity &&
              (*state.flight_velocity)[1] == 4.2F,
          "Start completion wins over simultaneous support loss");
  state = policy.landed(-4.2F, 9.8F);
  require(state.motion.requests[0].slot == 689 &&
              state.motion.requests[0].start_frames == 1,
          "Ordinary descent selects matching normal landing");
  state = policy.advance_landing(false, true, true);
  require(
      state.phase == ScriptedJumpPhase::inactive && state.cancelled_slot == 689,
      "Landing cancellation wins over support loss");
  static_cast<void>(policy.begin({0, {2, 4.2F, -3}}));
  state = policy.advance_start(false, true);
  require(state.flight_velocity == bw::math::Vector3<float>{2, 0, -3},
          "Start support loss retains horizontal capture and clears impulse");
  state = policy.landed(-10, 9.8F);
  require(state.motion.requests[0].slot == 704 &&
              state.motion.requests[0].start_frames == 0 &&
              state.motion.requests[0].blend_seconds == .3,
          "Hard landing has independent threshold and frame0 blend");
  state = policy.advance_landing(false, false, true);
  require(state.motion.requests[0].slot == 656 &&
              state.flight_velocity == bw::math::Vector3<float>{},
          "Landing support loss constructs fresh default StateJump");
  require(policy.landed(-1, 9.8F).phase == ScriptedJumpPhase::inactive,
          "Shallow supported descent returns directly to default owner");
  static_cast<void>(policy.begin({8, {0, 4.2F, 0}}));
  static_cast<void>(policy.advance_start(true, false));
  state = policy.landed(-4, 9.8F);
  require(state.motion.requests[0].slot == 697 && state.align_with_camera,
          "Raised landing collapses source directional family to9");
  const auto flight = bw::game::scripted_flight_step({2, 4, -3}, .1F, 10);
  require(std::abs(flight.displacement[1] - .35F) < 1e-6F &&
              flight.velocity[1] == 3,
          "Flight uses source trapezoidal gravity without root displacement");
  const auto corrected =
      bw::game::scripted_flight_feedback(flight.velocity, {0, .35F, -.3F}, .1F);
  require(std::abs(corrected[0] - 1.2F) < 1e-6F && corrected[1] == 3 &&
              std::abs(corrected[2] + 3) < 1e-6F,
          "Actual collision displacement feeds horizontal velocity at4dt");
  bool rejected = false;
  try {
    static_cast<void>(bw::game::scripted_flight_step({2, 4, -3}, 0, 10));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Paused clocks do not enter source division-by-dt path");
}
