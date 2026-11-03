#include "bw/runtime/engine.hpp"
#include <algorithm>
#include <cstdio>

// MEM.9: timing owns fixed-size state and performs no per-frame heap allocation.
namespace bw::engine {

// Engine::Engine
// Retain borrowed platform time/process services and application timing state
// with its defaults. Maximum FPS starts at 0 (uncapped), with no project
// override. Smoothing is disabled.
Engine::Engine(platform::PlatformTime& time, platform::PlatformProcess& process)
    : time_(time), process_(process) {}

// Engine::initialize_time
// Sample platform seconds into current application time.
// Do not set initial delta to zero or initialize last_real_time here: the
// previous-real timestamp is initialized at the first timing update instead.
void Engine::initialize_time() { application_time_.current_time = time_.seconds(); }

// requested_frame_wait
float requested_frame_wait(float maximum_fps, float real_delta) noexcept {
  return maximum_fps > 0.0F ? std::max(1.0F / maximum_fps - real_delta, 0.0F) : 0.0F;
}

// Engine::update_time_and_handle_max_tick_rate
// Ordinary real-time runtime client; smoothing off. Fixed-time, editor/server,
// world clamp, plugin rate handlers and timecode are outside slice.
void Engine::update_time_and_handle_max_tick_rate() {
  // First update and elapsed time.
  // On first call only set last_real_time to platform.seconds minus0.0001.
  // Copy current to last, sample actual current, and compute float real delta.
  if (!real_time_initialized_) {
    last_real_time_ = time_.seconds() - 0.0001;
    real_time_initialized_ = true;
  }
  application_time_.last_time = application_time_.current_time;
  double current_real_time = time_.seconds();
  application_time_.current_time = current_real_time;
  float real_delta = static_cast<float>(current_real_time - last_real_time_);

  // Negative real delta: warn and use0.01, without an exception or terminal policy.
  if (real_delta < 0.0F) {
    std::fputs("Negative real delta time; using 0.01 seconds\n", stderr);
    real_delta = 0.01F;
  }

  // Maximum rate and wait.
  // Positive configured maximum caps this unsmoothed mode; otherwise wait zero.
  const float requested_wait = requested_frame_wait(maximum_fps_, real_delta);
  double actual_idle_time = 0.0;
  if (requested_wait > 0.0F) {
    // Begin actual idle measurement and form the deadline from current real
    // time plus requested wait, not an epoch grid.
    const double idle_start = time_.seconds();
    const double deadline = current_real_time + static_cast<double>(requested_wait);

    // Strictly >5ms: sleep wait-2ms once.
    if (requested_wait > 0.005F) {
      process_.sleep(requested_wait - 0.002F);
    }
    // Yield to the deadline. No exit checks in this wait, repeated coarse
    // sleeps, learned slack or adaptive policy.
    while (time_.seconds() < deadline) {
      process_.sleep(0.0F);
    }

    // Sample actual wake time, publish it, then finish idle measurement.
    current_real_time = time_.seconds();
    application_time_.current_time = current_real_time;
    actual_idle_time = time_.seconds() - idle_start;
  }

  // Publication and rebase.
  // Measured elapsed/idle/overshoot; no wait means zero idle/overshoot.
  application_time_.delta_time = application_time_.current_time - last_real_time_;
  application_time_.idle_time = actual_idle_time;
  application_time_.idle_time_overshoot =
      std::max(actual_idle_time - static_cast<double>(requested_wait), 0.0);
  // Warn and substitute0.01 for a negative final delta.
  if (application_time_.delta_time < 0.0) {
    std::fputs("Negative application delta time; using 0.01 seconds\n", stderr);
    application_time_.delta_time = 0.01;
  }
  // Actual-time rebase; add delta to game time.
  last_real_time_ = current_real_time;
  application_time_.game_time += application_time_.delta_time;
}

// Engine::set_max_fps
// Store configured maximum rate; no floor, rounding or default override.
void Engine::set_max_fps(float maximum_fps) { maximum_fps_ = maximum_fps; }

// Engine::max_fps
// Read maximum-rate configuration. Nonpositive values disable this cap only.
float Engine::max_fps() const { return maximum_fps_; }

// Engine::application_time
// Expose const game-thread-owned time; task-context propagation is outside slice.
const ApplicationTime& Engine::application_time() const { return application_time_; }
} // namespace bw::engine
