#ifndef BW_PLATFORM_MACOS_PLATFORM_TIME_HPP
#define BW_PLATFORM_MACOS_PLATFORM_TIME_HPP
#include "bw/platform/time/platform_time.hpp"
namespace bw::platform::macos {
// Construct before engine startup: initialize mach_timebase_info; read Mach ticks.
class PlatformTime final : public platform::PlatformTime {
public:
  PlatformTime();
  [[nodiscard]] double seconds() const override;

private:
  double seconds_per_cycle_{};
};
class PlatformProcess final : public platform::PlatformProcess {
public:
  void sleep(float seconds) override;
};
// The result reports each API call. The scheduling operation reads the existing
// policy, then requests priority 45 using that policy. No QoS alternative.
struct SchedulingResult {
  int read_error{};
  int set_error{};
  int verify_error{};
  int policy{};
  int observed_priority{};
};
[[nodiscard]] SchedulingResult initialize_game_thread();
} // namespace bw::platform::macos
#endif
