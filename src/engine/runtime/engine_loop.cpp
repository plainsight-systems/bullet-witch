#include "engine/runtime/engine_loop.hpp"
#include "core/memory/allocator.hpp"
#include "core/tasks/named_thread_dispatch.hpp"
#include <cstdio>

// E.6: scope cleanup corresponds to GuardedMain's cleanup guard. MEM.9: loop
// accounting is fixed-size; no heap allocation in frame dispatch.
// Unreal reference: f45729c0b6f097038e72b52d366a62baeca3275b.
namespace bw::engine {

// EngineLoop::EngineLoop
// Reference: Engine/Source/Runtime/Core/Private/Misc/CoreGlobals.cpp:211,219,408;
// Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:4698-4740.
// Retain engine reference and optional seconds limit; counters/states start zero.
EngineLoop::EngineLoop(Engine& engine, double maximum_tick_time)
    : engine_(engine), maximum_tick_time_(maximum_tick_time) {}

// EngineLoop::pre_init
// Reference: Engine/Source/Runtime/Launch/Private/Launch.cpp:146-151;
// Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:2769-2890.
// Modules/config/input remain absent. Native platform time/thread precede entry.
int EngineLoop::pre_init() {
  // LaunchEngineLoop.cpp:1790: establish the game owner's allocator cache before
  // task startup; setup itself performs any required process-heap bootstrap.
  memory::setup_current_thread_cache();
  // LaunchEngineLoop.cpp:2592-2593: start task dispatch and attach game owner.
  // This starts only the accepted named-thread dependency, not worker pools.
  tasks::NamedThreadDispatch::instance().startup();
  return 0;
}

// EngineLoop::init
// Reference: Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:
// 4818-4830,4885-4890,4930-4934.
int EngineLoop::init() {
  // Engine-created boundary precedes InitTime; there is no UObject factory here.
  initialized_ = true;
  // Same reference:4818-4830. InitTime -> Engine Init.
  engine_.initialize_time();
  engine_.init(*this);
  // Same reference:4885-4890,4930-4934. Engine Start -> running.
  engine_.start();
  running_ = true;
  return 0;
}

// EngineLoop::tick
// Reference: Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:
// 5695-5710,5858-5861,6073-6076,6132-6135.
void EngineLoop::tick() {
  // Same reference:5695-5700. Nonzero seconds limit is checked before pacing;
  // its exit request still allows this frame to complete.
  if (maximum_tick_time_ != 0.0 && total_tick_time_ > maximum_tick_time_) {
    request_exit("EngineLoop seconds limit reached");
  }
  // Same reference:5702-5710. Update application time and handle maximum rate.
  engine_.update_time_and_handle_max_tick_rate();
  // Same reference:5858-5861. Pass measured float delta and BOOLEAN idle mode.
  // No world/render/input exists in this timing-only host.
  engine_.tick(static_cast<float>(engine_.application_time().delta_time), false);
  // Same reference:6073-6076. Ignore the first six frames in seconds accounting.
  if (frame_counter_ > 5) {
    total_tick_time_ += engine_.application_time().delta_time;
  }
  // Same reference:6132-6135. Complete one host frame, never a catch-up burst.
  ++frame_counter_;
}

// EngineLoop::exit
// Reference: Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:5010-5065.
// Clear running; call PreExit only after the engine-created boundary was reached.
// No renderer/audio/module cleanup is claimed before those systems exist.
void EngineLoop::exit() {
  running_ = false;
  if (initialized_) {
    engine_.pre_exit();
  }
  // LaunchEngineLoop.cpp:5215: shut down task dispatch after dependent engine
  // owners have stopped. None of the future render/worker owners exists yet.
  // The running guard covers callers that never reached task startup.
  auto& dispatch = tasks::NamedThreadDispatch::instance();
  if (dispatch.running()) {
    dispatch.shutdown();
  }
  // Game cache and allocator retain source process lifetime; no clear/destruction.
}

// EngineLoop::request_exit
// Reference: Engine/Source/Runtime/Core/Private/Misc/CoreGlobals.cpp:325-341.
// Log a meaningful reason and set the ordinary game-thread exit request. No
// cancellation of an in-progress sleep or engine tick is introduced.
void EngineLoop::request_exit(std::string_view reason) {
  if (reason.size() <= 4) {
    std::fputs("Engine exit reason should contain more than four characters\n", stderr);
  }
  std::fputs("Engine exit requested: ", stderr);
  std::fwrite(reason.data(), 1, reason.size(), stderr);
  std::fputc('\n', stderr);
  exit_requested_ = true;
}

// EngineLoop::exit_requested
// Reference: Engine/Source/Runtime/Core/Public/CoreGlobals.h:395-400.
// Expose request state to the outer loop on the game thread.
bool EngineLoop::exit_requested() const { return exit_requested_; }

// EngineLoop::is_running
// Reference: Engine/Source/Runtime/Core/Private/Misc/CoreGlobals.cpp:211;
// Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:4930-4934,5010-5015.
bool EngineLoop::is_running() const { return running_; }

// EngineLoop::frame_counter
// Reference: Engine/Source/Runtime/Core/Private/Misc/CoreGlobals.cpp:408;
// Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:6132-6135.
std::uint64_t EngineLoop::frame_counter() const { return frame_counter_; }

// run_engine_loop
// Reference: Engine/Source/Runtime/Launch/Private/Launch.cpp:66-72,123-194.
int run_engine_loop(EngineLoop& loop) {
  // Same reference:123-135 and66-72. Scope departure requests exit then cleans
  // up the loop. No exception translation or custom terminal policy is added.
  struct Cleanup {
    EngineLoop& loop;
    ~Cleanup() {
      loop.request_exit("Host engine lifecycle cleanup");
      loop.exit();
    }
  } cleanup{loop};

  // Same reference:146-151. PreInit errors/early exit skip initialization.
  const int pre_init_result = loop.pre_init();
  if (pre_init_result != 0 || loop.exit_requested()) {
    return pre_init_result;
  }
  // Same reference:164-194. Initialize, then repeat whole frames until requested exit.
  const int initialization_result = loop.init();
  while (!loop.exit_requested()) {
    loop.tick();
  }
  return initialization_result;
}
} // namespace bw::engine
