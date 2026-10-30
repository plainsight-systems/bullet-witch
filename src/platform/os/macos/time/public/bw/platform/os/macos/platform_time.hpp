#ifndef BW_PLATFORM_MACOS_PLATFORM_TIME_HPP
#define BW_PLATFORM_MACOS_PLATFORM_TIME_HPP
#include "bw/platform/time/platform_time.hpp"
namespace bw::platform::macos {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// Engine/Source/Runtime/Core/Private/Apple/ApplePlatformTime.cpp:29-43.
// Construct before engine startup: initialize mach_timebase_info; read Mach ticks.
class PlatformTime final : public platform::PlatformTime {
public:
  PlatformTime();
  [[nodiscard]] double seconds() const override;

private:
  double seconds_per_cycle_{};
};
// Same pin, Engine/Source/Runtime/Core/Private/GenericPlatform/
// GenericPlatformProcess.cpp:414-424, inherited by Mac through Apple.
class PlatformProcess final : public platform::PlatformProcess {
public:
  void sleep(float seconds) override;
};
// Observability of the actual API result is ours; the scheduling operation is
// Unreal's Engine/Source/Runtime/Core/Private/Mac/CocoaThread.cpp:358-369:
// read existing policy, request priority 45 using that policy. No QoS alternative.
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
