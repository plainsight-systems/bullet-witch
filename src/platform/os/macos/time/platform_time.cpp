#include "bw/platform/os/macos/platform_time.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

// MEM.9: Mach conversion and sleep/yield retain fixed-size state; no per-frame
// allocation. Policy comes from the reference, not this allocation guidance.
// Unreal reference: f45729c0b6f097038e72b52d366a62baeca3275b.
namespace bw::platform::macos {

// PlatformTime::PlatformTime
// Reference: Engine/Source/Runtime/Core/Private/Apple/ApplePlatformTime.cpp:29-36.
// Query mach_timebase_info and verify success; retain 1e-9*numer/denom as
// seconds per cycle. No steady_clock replacement or timer-coalescing adjustment.
// The host constructs this clock before entering the engine lifecycle.
PlatformTime::PlatformTime() {
  mach_timebase_info_data_t timebase{};
  if (mach_timebase_info(&timebase) != KERN_SUCCESS) {
    std::fputs("mach_timebase_info failed\n", stderr);
    std::abort();
  }
  seconds_per_cycle_ =
      1e-9 * static_cast<double>(timebase.numer) / static_cast<double>(timebase.denom);
}

// PlatformTime::seconds
// Reference: Engine/Source/Runtime/Core/Private/Apple/ApplePlatformTime.cpp:39-43;
// Engine/Source/Runtime/Core/Public/Apple/ApplePlatformTime.h:43-46.
// Read mach_absolute_time, convert to double seconds with the initialized
// scale, add16777216.0. The offset matches Apple reference timing exactly.
double PlatformTime::seconds() const {
  return static_cast<double>(mach_absolute_time()) * seconds_per_cycle_ + 16777216.0;
}

// PlatformProcess::sleep
// Reference: Engine/Source/Runtime/Core/Private/GenericPlatform/
// GenericPlatformProcess.cpp:414-424; inherited by Mac through ApplePlatformProcess.
// Multiply float seconds by1000000.0F, truncate to signed32-bit microseconds.
// When positive, call usleep once with that value; otherwise call sched_yield.
// No cancellation checks, condition variables, deadline API or alternate waiting.
void PlatformProcess::sleep(float seconds) {
  const auto microseconds = static_cast<std::int32_t>(seconds * 1000000.0F);
  if (microseconds > 0) {
    usleep(static_cast<useconds_t>(microseconds));
  } else {
    sched_yield();
  }
}

// initialize_game_thread
// Reference: Engine/Source/Runtime/Core/Private/Mac/CocoaThread.cpp:358-369.
// Initialize scheduler parameters, read current thread policy/parameters
// with pthread_getschedparam, assign priority45 and call pthread_setschedparam
// using the read policy. Preserve return codes. No guessed QoS or Windows timer policy.
SchedulingResult initialize_game_thread() {
  SchedulingResult result{};
  sched_param parameters{};
  int policy = SCHED_RR;
  result.read_error = pthread_getschedparam(pthread_self(), &policy, &parameters);
  parameters.sched_priority = 45;
  result.set_error = pthread_setschedparam(pthread_self(), policy, &parameters);

  // Observation of the same reference operation: read back policy/priority for
  // the host report. This does not change scheduling policy. Failed required API
  // calls are reported, never claimed to have successfully applied priority45.
  sched_param observed{};
  result.verify_error = pthread_getschedparam(pthread_self(), &policy, &observed);
  result.policy = policy;
  result.observed_priority = observed.sched_priority;
  return result;
}
} // namespace bw::platform::macos
