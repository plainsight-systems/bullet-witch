#include "bw/runtime/engine_loop.hpp"
#include "bw/memory/allocator.hpp"
#include "bw/tasks/named_thread_dispatch.hpp"
#include <cstdio>

// E.6: run_engine_loop cleans up through a scope guard. MEM.9: loop
// accounting is fixed-size; no heap allocation in frame dispatch.
namespace bw::engine {

// EngineLoop::EngineLoop
// Retain engine reference and optional seconds limit; counters/states start zero.
EngineLoop::EngineLoop(Engine& engine, double maximum_tick_time)
    : engine_(engine), maximum_tick_time_(maximum_tick_time) {}

// EngineLoop::pre_init
// Modules/config/input remain absent. Native platform time/thread precede entry.
int EngineLoop::pre_init() {
  // Establish the game owner's allocator cache before task startup; setup
  // itself performs any required process-heap bootstrap.
  memory::setup_current_thread_cache();
  // Start task dispatch and attach game owner.
  // This starts only the accepted named-thread dependency, not worker pools.
  tasks::NamedThreadDispatch::instance().startup();
  return 0;
}

// EngineLoop::init
int EngineLoop::init() {
  // Engine-created boundary precedes initialize_time; there is no object factory here.
  initialized_ = true;
  // initialize_time -> engine init.
  engine_.initialize_time();
  engine_.init(*this);
  // Engine start -> running.
  engine_.start();
  running_ = true;
  return 0;
}

// EngineLoop::tick
void EngineLoop::tick() {
  // Nonzero seconds limit is checked before pacing;
  // its exit request still allows this frame to complete.
  if (maximum_tick_time_ != 0.0 && total_tick_time_ > maximum_tick_time_) {
    request_exit("EngineLoop seconds limit reached");
  }
  // Update application time and handle maximum rate.
  engine_.update_time_and_handle_max_tick_rate();
  // Pass measured float delta and BOOLEAN idle mode.
  // No world/render/input exists in this timing-only host.
  engine_.tick(static_cast<float>(engine_.application_time().delta_time), false);
  // Ignore the first six frames in seconds accounting.
  if (frame_counter_ > 5) {
    total_tick_time_ += engine_.application_time().delta_time;
  }
  // Complete one host frame, never a catch-up burst.
  ++frame_counter_;
}

// EngineLoop::exit
// Clear running; call pre_exit only after the engine-created boundary was reached.
// No renderer/audio/module cleanup is claimed before those systems exist.
void EngineLoop::exit() {
  running_ = false;
  if (initialized_) {
    engine_.pre_exit();
  }
  // Shut down task dispatch after dependent engine owners have stopped. None of
  // the future render/worker owners exists yet.
  // The running guard covers callers that never reached task startup.
  auto& dispatch = tasks::NamedThreadDispatch::instance();
  if (dispatch.running()) {
    dispatch.shutdown();
  }
  // Game cache and allocator keep process lifetime; no clear/destruction.
}

// EngineLoop::request_exit
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
// Expose request state to the outer loop on the game thread.
bool EngineLoop::exit_requested() const { return exit_requested_; }

// EngineLoop::is_running
bool EngineLoop::is_running() const { return running_; }

// EngineLoop::frame_counter
std::uint64_t EngineLoop::frame_counter() const { return frame_counter_; }

// run_engine_loop
int run_engine_loop(EngineLoop& loop) {
  // Scope departure requests exit then cleans up the loop. No exception
  // translation or custom terminal policy is added.
  struct Cleanup {
    EngineLoop& loop;
    ~Cleanup() {
      loop.request_exit("Host engine lifecycle cleanup");
      loop.exit();
    }
  } cleanup{loop};

  // pre_init errors/early exit skip initialization.
  const int pre_init_result = loop.pre_init();
  if (pre_init_result != 0 || loop.exit_requested()) {
    return pre_init_result;
  }
  // Initialize, then repeat whole frames until requested exit.
  const int initialization_result = loop.init();
  while (!loop.exit_requested()) {
    loop.tick();
  }
  return initialization_result;
}
} // namespace bw::engine
