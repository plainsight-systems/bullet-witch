// Contract tests prepared before implementation for docs/architecture/game-loop.md.
// Assertions are unchanged from that preparation; these now link to production.
// Fakes implement platform/test-engine contracts only. They do not supply any
// definitions for Engine, EngineLoop or the native Mac production classes.
#include "engine/runtime/engine_loop.hpp"
#include "platform/macos/platform_time.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using bw::engine::ApplicationTime;
using bw::engine::Engine;
using bw::engine::EngineLoop;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void near(double observed, double expected, double tolerance, std::string_view message) {
  require(std::abs(observed - expected) <= tolerance, message);
}

// This deterministic platform records the production algorithm's primitive
// calls. Positive sleeps advance by their requested duration plus a chosen OS
// oversleep; zero sleeps advance a small test timeslice. No deadline policy lives here.
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

// A concrete test engine stands in for future world work. It records actual
// lifecycle/tick input; it does not control pacing or return scheduling decisions.
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

// Source defaults: AppTime.h:80-103; UnrealEngine.cpp:12136-12138.
void source_defaults() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  near(engine.max_fps(), 0.0, 0.0, "source maximum FPS default must be zero");
  const auto& time = engine.application_time();
  near(time.current_time, 0.0, 0.0, "current time default");
  near(time.last_time, 0.0, 0.0, "last time default");
  near(time.delta_time, 1.0 / 30.0, 0.0, "source delta default is 1/30");
  near(time.game_time, 0.0, 0.0, "game time default");
  near(time.idle_time, 0.0, 0.0, "idle time default");
  near(time.idle_time_overshoot, 0.0, 0.0, "idle overshoot default");
}

// Launch.cpp:123-194; LaunchEngineLoop.cpp:4818-4998,5010-5065,6132-6135.
void lifecycle_order() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  EngineLoop loop(engine);
  require(bw::engine::run_engine_loop(loop) == 0, "successful host result");
  require(engine.events == std::vector<std::string>{"init", "start", "tick", "pre_exit"},
          "PreInit/Init/Start/Tick/Exit must preserve source order");
  near(engine.init_time.current_time, 100.0, 0.0, "InitTime precedes Engine Init");
  near(engine.init_time.delta_time, 1.0 / 30.0, 0.0, "InitTime does not fabricate delta");
  require(!engine.init_saw_running && !engine.start_saw_running && engine.tick_saw_running &&
              !engine.pre_exit_saw_running,
          "running state follows source Init and Exit boundaries");
  require(loop.frame_counter() == 1 && engine.counter_at_exit_request == 0,
          "exit request in tick still completes that frame");
  require(loop.exit_requested() && !loop.is_running(), "host exit state");
}

// UnrealEngine.cpp:2999-3001: the first real-time update subtracts0.0001.
void uncapped_first_delta() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  EngineLoop loop(engine);
  require(bw::engine::run_engine_loop(loop) == 0, "host result");
  near(engine.frames.front().delta_time, 0.0001, 1e-10,
       "first delta follows source initialization");
  require(clock.sleep_calls.empty(), "source cap0 does not request waiting");
}

// UnrealEngine.cpp:3064,3103,3116,3121-3127. 100 is a test input, not a default.
void first_frame_is_paced() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.set_max_fps(100.0F);
  EngineLoop loop(engine);
  require(bw::engine::run_engine_loop(loop) == 0, "host result");
  require(clock.sleep_calls.size() > 1, "first frame includes coarse wait and yield tail");
  near(clock.sleep_calls.front(), 0.0079, 1e-8, "first wait includes source initial elapsed");
  for (std::size_t i = 1; i < clock.sleep_calls.size(); ++i) {
    near(clock.sleep_calls[i], 0.0, 0.0, "tail must use zero-second yield calls only");
  }
  require(engine.frames.front().delta_time >= 0.01, "first dispatch is paced, not zero delta");
  near(engine.tick_deltas.front(), engine.frames.front().delta_time, 1e-8,
       "engine work receives measured float delta");
  require(!engine.idle_modes.front(), "idle duration must not be converted into idle-mode bool");
}

// LaunchEngineLoop.cpp:5702-5710,5858-5861: prior work consumes the next budget.
void work_consumes_budget() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.set_max_fps(100.0F);
  engine.work_seconds = 0.003;
  engine.exit_after_ticks = 2;
  EngineLoop loop(engine);
  require(loop.init() == 0, "init result");
  loop.tick();
  clock.sleep_calls.clear();
  loop.tick();
  require(!clock.sleep_calls.empty(), "second frame still has time to wait");
  near(clock.sleep_calls.front(), 0.005, 1e-8, "10ms interval minus3ms work minus2ms tail");
  near(engine.frames.back().delta_time, 0.01, 0.00011, "delta includes work plus wait");
  loop.exit();
}

// UnrealEngine.cpp:3064,3155,3166: late work gets one measured frame, no catchup.
void late_frame_rebases() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.set_max_fps(100.0F);
  engine.work_seconds = 0.025;
  engine.exit_after_ticks = 3;
  EngineLoop loop(engine);
  require(loop.init() == 0, "init result");
  loop.tick();
  clock.sleep_calls.clear();
  engine.work_seconds = 0.0;
  loop.tick();
  require(clock.sleep_calls.empty() && loop.frame_counter() == 2,
          "over-budget frame neither waits nor dispatches compensating frames");
  near(engine.frames.back().delta_time, 0.025, 1e-10, "late actual elapsed is preserved");
  loop.tick();
  near(clock.sleep_calls.front(), 0.008, 1e-8, "next interval rebases to last actual frame");
  loop.exit();
}

// UnrealEngine.cpp:3103: strict >5ms, not >=5ms. 200 FPS is test input only.
void five_millisecond_threshold() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.update_time_and_handle_max_tick_rate();
  engine.set_max_fps(200.0F);
  engine.update_time_and_handle_max_tick_rate();
  require(!clock.sleep_calls.empty(), "threshold wait yields");
  for (const float duration : clock.sleep_calls) {
    near(duration, 0.0, 0.0, "exact5ms does not use coarse sleep");
  }
}

// UnrealEngine.cpp:3116,3121-3127,3150-3166: oversleep skips tail, remains elapsed.
void coarse_oversleep_is_measured() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.update_time_and_handle_max_tick_rate();
  engine.set_max_fps(100.0F);
  clock.oversleep = 0.007;
  engine.update_time_and_handle_max_tick_rate();
  require(clock.sleep_calls.size() == 1, "late coarse wake needs no yielding or extra coarse wait");
  near(clock.sleep_calls.front(), 0.008, 1e-8, "source2ms reserve");
  const auto& time = engine.application_time();
  near(time.delta_time, 0.015, 1e-8, "actual overslept delta");
  near(time.idle_time, 0.015, 1e-8, "actual wait measured");
  near(time.idle_time_overshoot, 0.005, 1e-8, "overshoot excludes requested wait");
}

// UnrealEngine.cpp:12193-12265: a changed cap is read by the next update.
void rate_setting_changes_next_wait() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.set_max_fps(100.0F);
  engine.update_time_and_handle_max_tick_rate();
  clock.sleep_calls.clear();
  engine.set_max_fps(50.0F);
  engine.update_time_and_handle_max_tick_rate();
  near(engine.max_fps(), 50.0, 0.0, "setting retained");
  near(clock.sleep_calls.front(), 0.018, 1e-8, "next update uses changed test cap");
}

// UnrealEngine.cpp:12136-12138,12236-12250; BaseEngine.ini:351 smoothingfalse.
void nonpositive_setting_disables_cap() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  for (const float cap : {0.0F, -1.0F}) {
    engine.set_max_fps(cap);
    engine.update_time_and_handle_max_tick_rate();
  }
  require(clock.sleep_calls.empty(), "nonpositive rate disables this cap, not future vsync");
}

// UnrealEngine.cpp:3009,3155-3166,3220: logical last/current and cumulative time.
void application_time_publication() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  EngineLoop loop(engine);
  require(loop.init() == 0, "init result");
  engine.update_time_and_handle_max_tick_rate();
  const auto first = engine.application_time();
  clock.current += 0.025;
  engine.update_time_and_handle_max_tick_rate();
  const auto& second = engine.application_time();
  near(second.last_time, first.current_time, 0.0, "last logical time from previous update");
  near(second.current_time, clock.current, 0.0, "current logical time is real time");
  near(second.delta_time, 0.025, 1e-10, "real delta preserved");
  near(second.game_time, first.game_time + second.delta_time, 0.0, "game time accumulates delta");
  near(second.idle_time, 0.0, 0.0, "no wait has zero idle");
  near(second.idle_time_overshoot, 0.0, 0.0, "no wait has zero overshoot");
  loop.exit();
}

// UnrealEngine.cpp:2883-2886,3160-3164: source fallback, no invented exception.
void negative_time_uses_source_fallback() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.update_time_and_handle_max_tick_rate();
  clock.current -= 1.0;
  engine.update_time_and_handle_max_tick_rate();
  near(engine.application_time().delta_time, 0.01, 0.0, "negative final delta source fallback");
}

// Launch.cpp:188-194 does not abort a frame inside its primitive waiting phase.
void exit_request_does_not_interrupt_frame() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.set_max_fps(100.0F);
  EngineLoop loop(engine);
  clock.request_during_sleep = &loop;
  require(bw::engine::run_engine_loop(loop) == 0, "host result");
  require(loop.frame_counter() == 1 && engine.frames.size() == 1,
          "request during wait still completes current engine tick");
  require(engine.frames.front().delta_time >= 0.01, "exit request did not shorten wait");
}

// Launch.cpp:149-151: exit before Init skips engine creation/work.
void requested_exit_before_startup() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  EngineLoop loop(engine);
  loop.request_exit("test before host startup");
  require(bw::engine::run_engine_loop(loop) == 0, "pre-request result");
  require(engine.events.empty() && loop.frame_counter() == 0, "no engine init/tick before exit");
}

// Launch.cpp:188-194: Start may request exit before the first outer iteration.
void requested_exit_during_start() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.exit_on_start = true;
  EngineLoop loop(engine);
  require(bw::engine::run_engine_loop(loop) == 0, "start-exit result");
  require(engine.events == std::vector<std::string>{"init", "start", "pre_exit"},
          "start-exit reaches cleanup without a tick");
}

// LaunchEngineLoop.cpp:5695-5700,6073-6076: skip first6frames, test limit next tick.
void source_seconds_limit_accounting() {
  RecordingPlatform clock;
  RecordingEngine engine(clock);
  engine.work_seconds = 0.01;
  engine.exit_after_ticks = 100;
  EngineLoop loop(engine, 0.015);
  require(bw::engine::run_engine_loop(loop) == 0, "seconds-limit result");
  require(loop.frame_counter() == 9 && engine.frames.size() == 9,
          "seconds limit skips six frames and finishes the frame requesting exit");
}

// ApplePlatformTime.cpp:29-43, ApplePlatformTime.h:43-46. Native primitive check.
void native_clock_and_sleep() {
  bw::platform::macos::PlatformTime clock;
  bw::platform::macos::PlatformProcess process;
  const double before = clock.seconds();
  process.sleep(0.001F);
  const double after = clock.seconds();
  require(before >= 16777216.0 && after >= before,
          "Mach time includes source offset and progresses");
  process.sleep(0.0F);
  require(clock.seconds() >= after, "native yield preserves monotonic observation");
}

using ScenarioFunction = void (*)();
struct Scenario {
  std::string_view name;
  ScenarioFunction run;
};
constexpr std::array scenarios{
    Scenario{"source_defaults", source_defaults},
    Scenario{"lifecycle_order", lifecycle_order},
    Scenario{"uncapped_first_delta", uncapped_first_delta},
    Scenario{"first_frame_is_paced", first_frame_is_paced},
    Scenario{"work_consumes_budget", work_consumes_budget},
    Scenario{"late_frame_rebases", late_frame_rebases},
    Scenario{"five_millisecond_threshold", five_millisecond_threshold},
    Scenario{"coarse_oversleep_is_measured", coarse_oversleep_is_measured},
    Scenario{"rate_setting_changes_next_wait", rate_setting_changes_next_wait},
    Scenario{"nonpositive_setting_disables_cap", nonpositive_setting_disables_cap},
    Scenario{"application_time_publication", application_time_publication},
    Scenario{"negative_time_uses_source_fallback", negative_time_uses_source_fallback},
    Scenario{"exit_request_does_not_interrupt_frame", exit_request_does_not_interrupt_frame},
    Scenario{"requested_exit_before_startup", requested_exit_before_startup},
    Scenario{"requested_exit_during_start", requested_exit_during_start},
    Scenario{"source_seconds_limit_accounting", source_seconds_limit_accounting},
    Scenario{"native_clock_and_sleep", native_clock_and_sleep},
};
} // namespace

// Test entry: runs one named contract scenario against the production library.
int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Provide one scenario name\n";
    return 2;
  }
  for (const auto& scenario : scenarios) {
    if (scenario.name == argv[1]) {
      try {
        scenario.run();
        return 0;
      } catch (const std::exception& error) {
        std::cerr << scenario.name << ": " << error.what() << '\n';
        return 1;
      }
    }
  }
  std::cerr << "Unknown scenario\n";
  return 2;
}
