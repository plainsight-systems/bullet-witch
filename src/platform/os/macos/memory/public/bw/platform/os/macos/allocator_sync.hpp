#ifndef BW_PLATFORM_MACOS_ALLOCATOR_SYNC_HPP
#define BW_PLATFORM_MACOS_ALLOCATOR_SYNC_HPP

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <os/lock.h>

// Core CP.2/CP.50: native ownership and guarded event state stay together.
// Core CP.44: named guards retain event lifetime through notification.
// Performance CONC.4: retain the bounded-spin/blocking protocol.
namespace bw::platform::macos {
#if defined(__aarch64__) || defined(__arm64__)
inline constexpr std::size_t allocator_cache_line_size = 128;
#else
inline constexpr std::size_t allocator_cache_line_size = 64;
#endif

// Native CPU pause used by the word-mutex bounded spin; not an OS yield.
void allocator_spin_yield();

class AllocatorRecursiveMutex final {
public:
  AllocatorRecursiveMutex();
  AllocatorRecursiveMutex(const AllocatorRecursiveMutex&) = delete;
  AllocatorRecursiveMutex& operator=(const AllocatorRecursiveMutex&) = delete;
  void lock();
  [[nodiscard]] bool try_lock();
  void unlock();

private:
  os_unfair_lock mutex_ = OS_UNFAIR_LOCK_INIT;
  std::atomic<std::uint32_t> thread_id_{0};
  std::uint32_t depth_{};
};

// Single waiter/single notifier per reset cycle; notification retains lock
// until all event accesses are finished.
class AllocatorManualResetEvent final {
public:
  AllocatorManualResetEvent();
  AllocatorManualResetEvent(const AllocatorManualResetEvent&) = delete;
  AllocatorManualResetEvent& operator=(const AllocatorManualResetEvent&) = delete;
  void reset();
  void wait();
  void notify();

private:
  std::mutex mutex_;
  std::condition_variable condition_;
  bool waiting_{true};
};
} // namespace bw::platform::macos

#endif
