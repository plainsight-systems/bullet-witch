#include "engine/runtime/engine.hpp"
#include <algorithm>
#include <cstdio>

// MEM.9: timing owns fixed-size state and performs no per-frame heap allocation.
// Unreal reference: f45729c0b6f097038e72b52d366a62baeca3275b.
namespace bw::engine {

// Engine::Engine
// Reference: Engine/Source/Runtime/Core/Public/Misc/AppTime.h:80-103;
// Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp:12136-12138.
// Retain borrowed platform time/process services and application timing state
// with reference defaults. Maximum FPS starts at source cvar default0, with no
// project override. Engine/Config/BaseEngine.ini:351 disables smoothing.
Engine::Engine(platform::PlatformTime& time, platform::PlatformProcess& process)
    : time_(time), process_(process) {}

// Engine::initialize_time
// Reference: Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp:4698-4705.
// Sample platform seconds into current application time during InitTime.
// Do not set initial delta to zero or initialize last_real_time here: reference
// initializes its previous-real timestamp at the first timing update instead.
void Engine::initialize_time() { application_time_.current_time = time_.seconds(); }

// Engine::update_time_and_handle_max_tick_rate
// Reference: Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp:2980-3220.
// Ordinary real-time runtime client; source-default smoothingfalse. Fixed-time,
// editor/server, world clamp, plugin rate handlers and timecode are outside slice.
void Engine::update_time_and_handle_max_tick_rate() {
  // First update and elapsed time, same reference:2999-3051.
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

  // CorrectNegativeTimeDelta, same reference:2883-2886,3049-3051:
  // warn and use0.01, without an invented exception or terminal policy.
  if (real_delta < 0.0F) {
    std::fputs("Negative real delta time; using 0.01 seconds\n", stderr);
    real_delta = 0.01F;
  }

  // Maximum rate and wait, same reference:3058-3064, GetMaxTickRate:12193-12250.
  // Positive configured maximum caps this unsmoothed mode; otherwise wait zero.
  const float requested_wait =
      maximum_fps_ > 0.0F ? std::max(1.0F / maximum_fps_ - real_delta, 0.0F) : 0.0F;
  double actual_idle_time = 0.0;
  if (requested_wait > 0.0F) {
    // Same reference:3080-3087. Begin actual idle measurement and form the
    // deadline from current real time plus requested wait, not an epoch grid.
    const double idle_start = time_.seconds();
    const double deadline = current_real_time + static_cast<double>(requested_wait);

    // Same reference:3103,3116. Strictly >5ms: sleep wait-2ms once.
    if (requested_wait > 0.005F) {
      process_.sleep(requested_wait - 0.002F);
    }
    // Same reference:3121-3124. Yield to the deadline. No exit checks in this
    // wait, repeated coarse sleeps, learned slack or adaptive policy.
    while (time_.seconds() < deadline) {
      process_.sleep(0.0F);
    }

    // Same reference:3127-3139 and scoped idle counter at3086.
    // Sample actual wake time, publish it, then finish idle measurement.
    current_real_time = time_.seconds();
    application_time_.current_time = current_real_time;
    actual_idle_time = time_.seconds() - idle_start;
  }

  // Publication and rebase, same reference:3150-3166,3220.
  // Measured elapsed/idle/overshoot; no wait means zero idle/overshoot.
  application_time_.delta_time = application_time_.current_time - last_real_time_;
  application_time_.idle_time = actual_idle_time;
  application_time_.idle_time_overshoot =
      std::max(actual_idle_time - static_cast<double>(requested_wait), 0.0);
  // Same reference:3160-3164. Warn and substitute0.01 for a negative final delta.
  if (application_time_.delta_time < 0.0) {
    std::fputs("Negative application delta time; using 0.01 seconds\n", stderr);
    application_time_.delta_time = 0.01;
  }
  // Same reference:3166,3220. Actual-time rebase; add delta to game time.
  last_real_time_ = current_real_time;
  application_time_.game_time += application_time_.delta_time;
}

// Engine::set_max_fps
// Reference: Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp:12255-12265.
// Store configured maximum rate; no invented floor, rounding or default override.
void Engine::set_max_fps(float maximum_fps) { maximum_fps_ = maximum_fps; }

// Engine::max_fps
// Reference: Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp:12250-12253.
// Read maximum-rate configuration. Nonpositive values disable this cap only.
float Engine::max_fps() const { return maximum_fps_; }

// Engine::application_time
// Reference: Engine/Source/Runtime/Core/Public/Misc/AppTime.h:80-103;
// Engine/Source/Runtime/Core/Private/Misc/AppTime.cpp:68-72.
// Expose const game-thread-owned time; task-context propagation is outside slice.
const ApplicationTime& Engine::application_time() const { return application_time_; }
} // namespace bw::engine
