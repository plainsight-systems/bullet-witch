#ifndef BW_PLATFORM_PLATFORM_TIME_HPP
#define BW_PLATFORM_PLATFORM_TIME_HPP
// I.1: explicit platform time/process dependencies. MEM.9: neither interface
// requires per-frame allocation.
namespace bw::platform {
// seconds() is a double-precision platform reading, including a fixed offset.
class PlatformTime {
public:
  virtual ~PlatformTime() = default;
  [[nodiscard]] virtual double seconds() const = 0;
};
// Positive microseconds sleep; zero yields.
// There is no interruptible deadline/cancellation contract in this interface.
class PlatformProcess {
public:
  virtual ~PlatformProcess() = default;
  virtual void sleep(float seconds) = 0;
};
} // namespace bw::platform
#endif
