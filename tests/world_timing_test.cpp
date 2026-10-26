// In-process world timing and mission admission contracts; no device or I/O.
// Fixtures own clocks/control state and retain observations only while the
// issuing world exists. Synthetic monotonic samples exercise publication,
// rejection, pause nesting and input routing through public interfaces.
// Algorithm: supply an interval or transition, observe admitted work and time,
// then verify rejection/next-frame behavior without inspecting private storage.
// Nine bounded cases use inline state and a fixed eight-entry pause sequence;
// no fixture heap storage or performance claim. Exceptions may allocate.
// Design and implementation consultations: Core E.4 (#re-design-invariants)
// preserves usable state after rejection; performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps fixture/control work bounded and
// avoids unrelated dynamic setup. See docs/architecture/world-timing.md.
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "bw/input/action_repeat.hpp"
#include "bw/timing/frame_clock.hpp"
#include "bw/world/world_clock.hpp"
#include "game/developer_review_flow.hpp"
#include "game/mission_control.hpp"
#include "game/stage_inspection.hpp"

namespace {
using bw::game::GameplayInput;
using bw::game::MissionControl;
using bw::game::ScriptPause;
using bw::timing::FrameClock;
using bw::world::WorldClock;
using namespace std::chrono_literals;

// Capability checks prevent a future convenience API from reopening mutation.
// Core I.4/Con.2 and COPY.5 become compile-time restrictions, not just
// citations.
template <class T>
constexpr bool stable_authority =
    !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T> &&
    !std::is_move_constructible_v<T> && !std::is_move_assignable_v<T>;
template <class T>
concept HasStagePauseMutation = requires(T& value) { value.set_paused(true); };
template <class T>
concept HasScriptPauseMutation =
    requires(T& value) { value.push_script_pause(ScriptPause::simulation); };
template <class T>
concept HasInputMutation =
    requires(T& value) { value.restrict_hardware_input(true); };
template <class T>
concept HasInputRouting =
    requires(T& value) { value.route(GameplayInput{}, false); };
static_assert(stable_authority<FrameClock> && stable_authority<WorldClock> &&
              stable_authority<MissionControl>);
static_assert(!HasStagePauseMutation<bw::game::StageInspection>);
static_assert(!HasScriptPauseMutation<bw::game::UserPauseControl> &&
              !HasInputMutation<bw::game::UserPauseControl> &&
              !HasInputRouting<bw::game::UserPauseControl>);
static_assert(!std::is_constructible_v<bw::game::DeveloperReviewFlow,
                                       MissionControl&, unsigned>);
static_assert(std::is_constructible_v<bw::game::DeveloperReviewFlow,
                                      bw::game::UserPauseControl, unsigned>);
static_assert(
    !std::is_convertible_v<bw::game::UserPauseControl, MissionControl&>);

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template <class Operation>
void require_rejected(Operation operation, const char* message) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, message);
}
bool near(double left, double right) { return std::abs(left - right) < 1e-6; }
bool neutral(const GameplayInput& input) {
  return input.movement == std::array<float, 2>{} &&
         input.look == std::array<float, 2>{} && input.fire_trigger == 0 &&
         !input.evade_held && !input.crouch_pressed && !input.reload_pressed &&
         !input.attack_pressed && !input.weapon_cycle_pressed &&
         !input.aim_mode_pressed;
}
GameplayInput active_input() {
  GameplayInput input;
  input.movement = {0.75F, -0.5F};
  input.look = {-0.25F, 0.5F};
  input.fire_trigger = 0.8F;
  input.evade_held = true;
  input.crouch_pressed = true;
  input.reload_pressed = true;
  input.attack_pressed = true;
  input.weapon_cycle_pressed = true;
  input.aim_mode_pressed = true;
  return input;
}

// The recovered cap applies before world speed, never to the scaled result.
void presentation_and_world_intervals() {
  const FrameClock::Clock::time_point start{};
  FrameClock frame_clock(start, 10ms);
  WorldClock world;
  require(world.scale() == 1, "world starts at recovered normal speed");
  world.set_scale(2);
  const auto timing = frame_clock.sample(start + 500ms);
  const auto normal = world.publish(timing.presentation_seconds, {});
  require(near(timing.wall_seconds, 0.5) &&
              near(timing.presentation_seconds, 0.1) &&
              near(normal.delta_seconds(), 0.2) &&
              near(normal.simulation_seconds(), 0.2),
          "world scales bounded presentation time without a second cap");
  const auto control = world.publish(0.05F, {false, false});
  require(near(control.delta_seconds(), 0.1) &&
              near(control.simulation_seconds(), normal.simulation_seconds()) &&
              !control.admission().simulation &&
              !control.admission().hardware_input,
          "control work retains scaled delta without advancing simulation");
  const auto resumed = world.publish(0.025F, {});
  require(near(resumed.simulation_seconds(), 0.25),
          "resume advances only its own admitted interval");
}

// A missed deadline and an explicit loading rebase do not create catch-up work.
void deadlines_and_loading_rebase() {
  const FrameClock::Clock::time_point start{};
  FrameClock clock(start, 10ms);
  require(clock.deadline() == start && clock.sample(start).wall_seconds == 0 &&
              clock.deadline() == start + 10ms,
          "startup presents immediately without inventing elapsed time");
  const auto first = clock.sample(start + 10ms);
  require(near(first.wall_seconds, 0.01) && clock.deadline() == start + 20ms,
          "on-time pacing advances one presentation deadline");
  const auto missed = clock.sample(start + 1010ms);
  require(near(missed.wall_seconds, 1) &&
              near(missed.presentation_seconds, 0.1) &&
              clock.deadline() == start + 1020ms,
          "late presentation rebases its deadline without catch-up frames");
  const auto next = clock.sample(start + 1020ms);
  require(near(next.presentation_seconds, 0.01),
          "discarded elapsed time is not replayed into later frames");
  clock.rebase(start + 5s);
  require(clock.deadline() == start + 5010ms,
          "resource boundary rebases presentation pacing");
  const auto loaded = clock.sample(start + 5010ms);
  require(near(loaded.wall_seconds, 0.01),
          "loading duration is excluded from the next sampled interval");
}

void rejection_and_retained_observations() {
  const FrameClock::Clock::time_point start{};
  require_rejected([&] { FrameClock invalid(start, 0ms); },
                   "nonpositive pacing period rejected");
  FrameClock clock(start, 10ms);
  clock.sample(start + 10ms);
  const auto deadline = clock.deadline();
  require_rejected([&] { clock.sample(start + 9ms); },
                   "backward sample rejected");
  require_rejected([&] { clock.rebase(start + 9ms); },
                   "backward rebase rejected");
  require(clock.deadline() == deadline &&
              near(clock.sample(start + 20ms).wall_seconds, 0.01),
          "rejection preserves pacing deadline and previous sample");

  WorldClock world;
  WorldClock other;
  const auto retained = world.publish(0.05F, {});
  for (const float invalid : {-1.0F, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
    require_rejected([&] { world.set_scale(invalid); },
                     "invalid world speed rejected");
    require_rejected([&] { world.publish(invalid, {}); },
                     "invalid presentation interval rejected");
  }
  require(world.scale() == 1 &&
              world.simulation_seconds() == retained.simulation_seconds(),
          "rejected world changes preserve scale and accumulated time");
  world.set_scale(0.5F);
  const auto next = world.publish(0.05F, {});
  require(
      next.sequence() == retained.sequence() + 1 &&
          near(next.delta_seconds(), 0.025) &&
          near(retained.delta_seconds(), 0.05) &&
          near(retained.simulation_seconds(), 0.05) &&
          retained.admission().simulation && retained.belongs_to(world) &&
          !retained.belongs_to(other),
      "published frame retains identity, admission and interval after changes");
  world.set_scale(0);
  const auto stopped = world.publish(0.05F, {});
  require(stopped.delta_seconds() == 0 &&
              stopped.simulation_seconds() == next.simulation_seconds(),
          "zero speed publishes a zero interval without inventing pause state");
}

// A copied menu handle shares one owner; it cannot bypass script-owned
// admission.
void menu_pause_capability() {
  MissionControl control;
  auto first = control.user_pause_control();
  auto second = first;
  require(first.set_user_paused(true) && control.user_paused() &&
              second.user_paused(),
          "menu handles must observe the same user-pause authority");
  require(second.set_user_paused(false) && !first.user_paused(),
          "resume through another handle must publish to the same owner");
  control.push_script_pause(ScriptPause::simulation);
  require(!first.set_user_paused(true) && !control.user_paused() &&
              control.simulation_paused(),
          "menu requests must preserve script-owned pause admission");
  control.pop_script_pause();
  require(second.set_user_paused(true) && first.user_paused(),
          "restored menu admission must reach the original owner");
}

void independent_pause_admission() {
  MissionControl control;
  require(
      control.admission().simulation && control.admission().hardware_input &&
          control.pause_requests_allowed() && control.mission_script_admitted(),
      "fresh mission admits world, scripts and hardware input");
  require(control.set_user_paused(true) && control.user_paused() &&
              !control.pause_requests_allowed() &&
              !control.admission().simulation &&
              !control.admission().hardware_input && control.audio_paused() &&
              !control.mission_script_admitted(),
          "user pause excludes simulation and mission coroutine");
  require(control.set_user_paused(true) && control.set_user_paused(false) &&
              control.pause_requests_allowed(),
          "same-state pause is inert and resume restores menu admission");

  control.push_script_pause(ScriptPause::simulation);
  require(
      control.simulation_paused() && control.audio_paused() &&
          !control.admission().simulation &&
          control.mission_script_admitted() &&
          !control.pause_requests_allowed() && !control.set_user_paused(true),
      "script pause retains control-script admission and blocks menu requests");
  control.pop_script_pause();
  control.push_script_pause(ScriptPause::input);
  require(control.admission().simulation &&
              !control.admission().hardware_input && !control.audio_paused() &&
              control.mission_script_admitted(),
          "pad pause neutralizes input while world and script work continue");
  control.pop_script_pause();
  control.restrict_hardware_input(true);
  require(control.admission().simulation &&
              !control.admission().hardware_input &&
              control.pause_requests_allowed(),
          "additional hardware restriction does not become game pause");
  control.restrict_hardware_input(false);
  require(control.admission().hardware_input, "hardware restriction can clear");
}

void nested_restore_excludes_user_pause() {
  MissionControl nested;
  nested.push_script_pause(ScriptPause::input);
  nested.push_script_pause(ScriptPause::simulation);
  nested.pop_script_pause();
  require(nested.admission().simulation && !nested.admission().hardware_input &&
              !nested.pause_requests_allowed(),
          "inner script resume restores outer pad restriction");
  nested.pop_script_pause();
  require(nested.admission().simulation && nested.admission().hardware_input &&
              nested.pause_requests_allowed(),
          "outer resume restores original script flags");

  MissionControl independent;
  require(independent.set_user_paused(true), "user enters pause");
  independent.push_script_pause(ScriptPause::input);
  require(independent.set_user_paused(false),
          "user leaves pause during script scope");
  independent.pop_script_pause();
  require(
      !independent.user_paused() && independent.admission().simulation &&
          independent.admission().hardware_input &&
          !independent.pause_requests_allowed(),
      "script restore excludes user state but restores saved menu admission");
  require(independent.set_user_paused(false) &&
              !independent.pause_requests_allowed(),
          "repeated resume cannot erase restored script admission");
}

void checked_pause_stack_preserves_state() {
  MissionControl control;
  for (int index = 0; index < 8; ++index) {
    control.push_script_pause(ScriptPause::input);
  }
  require_rejected(
      [&] { control.push_script_pause(ScriptPause::simulation); },
      "ninth pause scope rejected without native overflow mutation");
  require(control.admission().simulation &&
              !control.admission().hardware_input &&
              !control.pause_requests_allowed(),
          "overflow preserves the previous admitted work");
  for (int index = 0; index < 8; ++index) control.pop_script_pause();
  require_rejected([&] { control.pop_script_pause(); },
                   "unbalanced resume rejected");
  require(control.admission().simulation &&
              control.admission().hardware_input &&
              control.pause_requests_allowed(),
          "failed push and pop preserve the complete original stack state");
}

// UI owns transition-frame edges; held movement resumes but evade must release.
void neutral_routing_and_evade_rearm() {
  MissionControl control;
  const auto sampled = active_input();
  require(control.route(sampled, false).evade_held,
          "ordinary admitted input retains held evade");
  require(
      control.set_user_paused(true) && neutral(control.route(sampled, true)),
      "opening pause consumes every gameplay command");
  require(neutral(control.route(sampled, false)),
          "paused frames publish neutral gameplay input");
  require(control.set_user_paused(false) &&
              !control.admission(true).simulation &&
              !control.admission(true).hardware_input &&
              neutral(control.route(sampled, true)),
          "resume UI frame cannot leak its input or advance world simulation");

  GameplayInput held;
  held.movement = sampled.movement;
  held.look = sampled.look;
  held.fire_trigger = sampled.fire_trigger;
  held.evade_held = true;
  const auto resumed = control.route(held, false);
  require(resumed.movement == held.movement && resumed.look == held.look &&
              resumed.fire_trigger == held.fire_trigger &&
              !resumed.evade_held && !resumed.crouch_pressed &&
              !resumed.reload_pressed && !resumed.attack_pressed &&
              !resumed.weapon_cycle_pressed && !resumed.aim_mode_pressed,
          "resume restores held axes without replaying UI edges or held evade");
  held.evade_held = false;
  require(!control.route(held, false).evade_held,
          "release rearms without an evade");
  held.evade_held = true;
  require(control.route(held, false).evade_held,
          "new evade press after release is admitted");
  control.push_script_pause(ScriptPause::input);
  require(
      neutral(control.route(sampled, false)) && control.admission().simulation,
      "input-only pause delivers neutral commands without freezing world");
}
// Presentation repeat preserves the original first-sample and one-event policy.
void action_repeat_uses_supplied_presentation_time() {
  bw::input::ActionRepeat repeat;
  constexpr std::uint32_t held = 1U;
  constexpr std::uint32_t edge = 1U << 31U;
  require(repeat.advance({held, edge}, 0.25F) == edge,
          "press edges pass through while first held sample accumulates");
  require(repeat.advance({held, 0}, 0.25F) == held,
          "first held interval contributes to the initial repeat threshold");
  require(repeat.advance({held, 0}, 0) == 0,
          "repeat subtracts the source interval after its threshold event");
  require(repeat.advance({}, 0) == 0 && repeat.advance({held, 0}, 0.25F) == 0,
          "release resets accumulated repeat time");

  for (const float invalid : {-1.0F, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
    require_rejected([&] { repeat.advance({}, invalid); },
                     "invalid presentation intervals reject before release");
  }
  require(repeat.advance({held, 0}, 0.25F) == held,
          "rejected intervals preserve the previous held timer");

  bw::input::ActionRepeat delayed;
  require(delayed.advance({held, 0}, 1.0F) == held,
          "a delayed sample publishes one repeat event");
  for (int remaining = 0; remaining < 4; ++remaining) {
    require(delayed.advance({held, 0}, 0) == held,
            "one subtraction retains excess elapsed time for later samples");
  }
  require(delayed.advance({held, 0}, 0) == 0,
          "five source subtractions leave the delayed timer below threshold");
}
}  // namespace

void world_timing_contracts() {
  presentation_and_world_intervals();
  deadlines_and_loading_rebase();
  rejection_and_retained_observations();
  menu_pause_capability();
  independent_pause_admission();
  nested_restore_excludes_user_pause();
  checked_pause_stack_preserves_state();
  neutral_routing_and_evade_rearm();
  action_repeat_uses_supplied_presentation_time();
}
