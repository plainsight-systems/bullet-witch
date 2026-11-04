// Unit test: public contracts of bw/runtime/engine_loop.hpp and
// bw/runtime/engine.hpp, one scenario per CTest entry, all on a recording clock.
// The wait arithmetic itself is covered by frame_wait_tests; these scenarios
// prove the loop executes, measures and publishes that wait.
#include "bw/runtime/engine_loop.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {
using bw::engine::ApplicationTime;
using bw::engine::Engine;
using bw::engine::EngineLoop;

using bw::testing::require;

void near(double observed, double expected, double tolerance, std::string_view message) {
  require(std::abs(observed - expected) <= tolerance, message);
}

class RecordingPlatform final : public bw::platform::PlatformTime,
                                public bw::platform::PlatformProcess {
public:
  double current{100.0};
  double oversleep{};
  double yield_step{0.0001};
  std::vector<float> sleep_calls;
  EngineLoop* request_during_sleep{};
  [[nodiscard]] double seconds() const override { return current; }
  void sleep(float seconds) override {
    require(sleep_calls.size() < 10000, "test wait exceeded finite fake-clock budget");
    sleep_calls.push_back(seconds);
    if (request_during_sleep != nullptr) {
      request_during_sleep->request_exit("test exit during primitive sleep");
      request_during_sleep = nullptr;
    }
    current += seconds > 0.0F ? static_cast<double>(seconds) + oversleep : yield_step;
  }
};

class RecordingEngine final : public Engine {
public:
  explicit RecordingEngine(RecordingPlatform& platform)
      : Engine(platform, platform), clock(platform) {}
  RecordingPlatform& clock;
  EngineLoop* host{};
  std::vector<std::string> events;
  std::vector<ApplicationTime> frames;
  std::vector<float> tick_deltas;
  std::vector<bool> idle_modes;
  double work_seconds{};
  std::size_t exit_after_ticks{1};
  bool exit_on_start{};
  bool init_saw_running{};
  bool start_saw_running{};
  bool tick_saw_running{};
  bool pre_exit_saw_running{};
  ApplicationTime init_time{};
  std::uint64_t counter_at_exit_request{};
  void init(EngineLoop& loop) override {
    host = &loop;
    events.emplace_back("init");
    init_time = application_time();
    init_saw_running = loop.is_running();
  }
  void start() override {
    events.emplace_back("start");
    start_saw_running = host->is_running();
    if (exit_on_start) {
      host->request_exit("test exit during engine Start");
    }
  }
  void pre_exit() override {
    events.emplace_back("pre_exit");
    pre_exit_saw_running = host->is_running();
  }
  void tick(float delta_seconds, bool idle_mode) override {
    events.emplace_back("tick");
    frames.push_back(application_time());
    tick_deltas.push_back(delta_seconds);
    idle_modes.push_back(idle_mode);
    tick_saw_running = host->is_running();
    clock.current += work_seconds;
    if (frames.size() >= exit_after_ticks) {
      counter_at_exit_request = host->frame_counter();
      host->request_exit("test completed requested engine ticks");
    }
  }
};

// ApplicationTime's defaults are stated in engine.hpp.
void defaults() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  near(engine.max_fps(), 0.0, 0.0, "maximum FPS defaults to zero (no cap)");
  const auto& time = engine.application_time();
  near(time.current_time, 0.0, 0.0, "current time default");
  near(time.last_time, 0.0, 0.0, "last time default");
  near(time.delta_time, 1.0 / 30.0, 0.0, "delta default is 1/30");
  near(time.game_time, 0.0, 0.0, "game time default");
  near(time.idle_time, 0.0, 0.0, "idle time default");
  near(time.idle_time_overshoot, 0.0, 0.0, "idle overshoot default");
}

void lifecycle_order() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  EngineLoop loop(engine);
  require(bw::engine::run_engine_loop(loop) == 0, "successful host result");
  require(engine.events == std::vector<std::string>{"init", "start", "tick", "pre_exit"},
          "init, start, tick, pre_exit in that order");
  near(engine.init_time.current_time, 100.0, 0.0, "time is initialized before Engine init");
  require(!engine.init_saw_running && !engine.start_saw_running && engine.tick_saw_running &&
              !engine.pre_exit_saw_running,
          "is_running is true only during ticks");
  require(loop.frame_counter() == 1 && engine.counter_at_exit_request == 0,
          "an exit request in tick still completes that frame");
  require(loop.exit_requested() && !loop.is_running(), "exit state after the run");
}

void capped_frame_is_paced() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.set_max_fps(100.0F);
  EngineLoop loop(engine);
  static_cast<void>(bw::engine::run_engine_loop(loop));
  require(!clock.sleep_calls.empty(), "a cap waits");
  require(engine.frames.front().delta_time >= 0.01, "the first dispatch is paced to the cap");
  near(engine.tick_deltas.front(), engine.frames.front().delta_time, 1e-8,
       "tick receives the measured delta");
  require(!engine.idle_modes.front(), "waiting is not reported as idle mode");
}

void late_frame_is_not_compensated() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.set_max_fps(100.0F);
  engine.work_seconds = 0.025;
  engine.exit_after_ticks = 3;
  EngineLoop loop(engine);
  static_cast<void>(loop.init());
  loop.tick();
  engine.work_seconds = 0.0;
  loop.tick();
  require(loop.frame_counter() == 2, "an over-budget frame dispatches no catch-up frame");
  near(engine.frames.back().delta_time, 0.025, 1e-10, "the late frame's actual delta is kept");
  clock.sleep_calls.clear();
  loop.tick();
  require(!clock.sleep_calls.empty(), "the next frame paces again");
  loop.exit();
}

void oversleep_is_measured() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.update_time_and_handle_max_tick_rate();
  engine.set_max_fps(100.0F);
  clock.oversleep = 0.007;
  engine.update_time_and_handle_max_tick_rate();
  const auto& time = engine.application_time();
  near(time.delta_time, 0.015, 1e-8, "delta reports the actual elapsed time");
  near(time.idle_time, 0.015, 1e-8, "idle time reports the actual wait");
  require(time.idle_time_overshoot > 0.0 && time.idle_time_overshoot < time.idle_time,
          "overshoot is the part of the wait beyond the request");
}

void rate_setting_is_retained() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.set_max_fps(100.0F);
  engine.update_time_and_handle_max_tick_rate();
  engine.set_max_fps(50.0F);
  near(engine.max_fps(), 50.0, 0.0, "setting retained");
  clock.sleep_calls.clear();
  clock.current += 0.004;
  engine.update_time_and_handle_max_tick_rate();
  require(!clock.sleep_calls.empty(), "the changed cap is applied on the next update");
  // The primitive sleep is the requested wait less the 2 ms the engine leaves
  // for the yield loop (engine.hpp); 1/100 - 0.004 - 0.002 would be 0.004.
  near(clock.sleep_calls.front(), 1.0 / 50.0 - 0.004 - 0.002, 0.0002,
       "the first sleep after the change is the new interval less the frame's work");
}

void application_time_publication() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  EngineLoop loop(engine);
  static_cast<void>(loop.init());
  engine.update_time_and_handle_max_tick_rate();
  const auto first = engine.application_time();
  clock.current += 0.025;
  engine.update_time_and_handle_max_tick_rate();
  const auto& second = engine.application_time();
  near(second.last_time, first.current_time, 0.0, "last_time is the previous current_time");
  near(second.current_time, clock.current, 0.0, "current_time is the clock");
  near(second.delta_time, 0.025, 1e-10, "delta_time is the elapsed time");
  near(second.game_time, first.game_time + second.delta_time, 0.0, "game_time accumulates delta");
  near(second.idle_time, 0.0, 0.0, "no wait means zero idle");
  near(second.idle_time_overshoot, 0.0, 0.0, "no wait means zero overshoot");
  loop.exit();
}

void exit_request_does_not_interrupt_frame() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.set_max_fps(100.0F);
  EngineLoop loop(engine);
  clock.request_during_sleep = &loop;
  static_cast<void>(bw::engine::run_engine_loop(loop));
  require(loop.frame_counter() == 1 && engine.frames.size() == 1,
          "a request during the wait still completes the frame");
  require(engine.frames.front().delta_time >= 0.01, "the request did not shorten the wait");
}

void requested_exit_before_startup() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  EngineLoop loop(engine);
  loop.request_exit("test before host startup");
  require(bw::engine::run_engine_loop(loop) == 0, "pre-request result");
  require(engine.events.empty() && loop.frame_counter() == 0, "no init or tick before exit");
}

void requested_exit_during_start() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.exit_on_start = true;
  EngineLoop loop(engine);
  static_cast<void>(bw::engine::run_engine_loop(loop));
  require(engine.events == std::vector<std::string>{"init", "start", "pre_exit"},
          "an exit during start reaches pre_exit without a tick");
}

void seconds_limit_ends_the_run() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.work_seconds = 0.01;
  engine.exit_after_ticks = 100;
  EngineLoop loop(engine, 0.015);
  require(bw::engine::run_engine_loop(loop) == 0, "seconds-limit result");
  require(loop.frame_counter() == engine.frames.size() && engine.frames.size() < 100,
          "the limit ends the run before the engine's own exit");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{
    Scenario{"defaults", defaults},
    Scenario{"lifecycle_order", lifecycle_order},
    Scenario{"capped_frame_is_paced", capped_frame_is_paced},
    Scenario{"late_frame_is_not_compensated", late_frame_is_not_compensated},
    Scenario{"oversleep_is_measured", oversleep_is_measured},
    Scenario{"rate_setting_is_retained", rate_setting_is_retained},
    Scenario{"application_time_publication", application_time_publication},
    Scenario{"exit_request_does_not_interrupt_frame", exit_request_does_not_interrupt_frame},
    Scenario{"requested_exit_before_startup", requested_exit_before_startup},
    Scenario{"requested_exit_during_start", requested_exit_during_start},
    Scenario{"seconds_limit_ends_the_run", seconds_limit_ends_the_run},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
