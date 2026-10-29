#ifndef BW_PLATFORM_PLATFORM_TIME_HPP
#define BW_PLATFORM_PLATFORM_TIME_HPP
// I.1: explicit platform time/process dependencies. MEM.9: neither interface
// requires per-frame allocation. Algorithms come from pinned Unreal, not these rules.
namespace bw::platform {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// Engine/Source/Runtime/Core/Private/Apple/ApplePlatformTime.cpp:29-43.
// seconds() is a double-precision platform reading, including the Apple offset.
class PlatformTime {
public:
  virtual ~PlatformTime() = default;
  [[nodiscard]] virtual double seconds() const = 0;
};
// Same pin, Engine/Source/Runtime/Core/Private/GenericPlatform/
// GenericPlatformProcess.cpp:414-424. Positive microseconds sleep; zero yields.
// There is no interruptible deadline/cancellation contract in this interface.
class PlatformProcess {
public:
  virtual ~PlatformProcess() = default;
  virtual void sleep(float seconds) = 0;
};
} // namespace bw::platform
#endif
