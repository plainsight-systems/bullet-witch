#include "bw/platform/os/macos/allocator_sync.hpp"

#include <pthread.h>
#if defined(__x86_64__)
#include <immintrin.h>
#endif

// Core CP.2: first-use native entry publication is atomic. CP.44/CP.50: named
// event guards cover the condition and its notification lifetime. Performance
// CONC.4: preserve Unreal's bounded CPU spin followed by blocking, not a spinlock.
// Unreal reference: f45729c0b6f097038e72b52d366a62baeca3275b.
namespace bw::platform::macos {
namespace {
using LockFunction = void (*)(os_unfair_lock_t, os_unfair_lock_flags_t);

// Private/Apple/AppleRecursiveMutex.cpp:27-30, LockFallback: older systems keep
// the same unfair lock but cannot request the optional adaptive-spin flag.
void lock_fallback(os_unfair_lock_t mutex, os_unfair_lock_flags_t) { os_unfair_lock_lock(mutex); }

// Private/Apple/AppleRecursiveMutex.cpp:8-24, LockRuntimeDispatch: lazily select
// the available native entry. Adaptation: atomic publication replaces the
// source's mutable plain pointer, permitting simultaneous first allocator locks.
void lock_with_flags(os_unfair_lock_t mutex) {
  static std::atomic<LockFunction> selected{nullptr};
  auto implementation = selected.load(std::memory_order_relaxed);
  if (implementation == nullptr) {
    implementation = &os_unfair_lock_lock_with_flags != nullptr ? &os_unfair_lock_lock_with_flags
                                                                : &lock_fallback;
    selected.store(implementation, std::memory_order_relaxed);
  }
  implementation(mutex, OS_UNFAIR_LOCK_FLAG_ADAPTIVE_SPIN);
}
} // namespace

// Public/GenericPlatform/GenericPlatformProcess.h:964-978, Yield: issue the
// architecture pause instruction. This does not yield the OS time slice.
void allocator_spin_yield() {
#if defined(__aarch64__) || defined(__arm64__)
  __asm__ __volatile__("isb");
#elif defined(__x86_64__)
  _mm_pause();
#else
#error Unsupported Mac allocator CPU architecture
#endif
}

// Public/Apple/AppleRecursiveMutex.h:28-35: native unlocked state, no owner/depth.
AllocatorRecursiveMutex::AllocatorRecursiveMutex() = default;

// AppleRecursiveMutex.h:40-50, Lock: recursion belongs to the Mach thread ID;
// a different owner acquires the unfair lock before publishing itself.
void AllocatorRecursiveMutex::lock() {
  const auto current = pthread_mach_thread_np(pthread_self());
  if (thread_id_.load(std::memory_order_relaxed) != current) {
    lock_with_flags(&mutex_);
    thread_id_.store(current, std::memory_order_relaxed);
  }
  ++depth_;
}

// AppleRecursiveMutex.h:52-70, TryLock: existing owner recurses; other threads
// publish ownership only after native nonblocking acquisition succeeds.
bool AllocatorRecursiveMutex::try_lock() {
  const auto current = pthread_mach_thread_np(pthread_self());
  if (thread_id_.load(std::memory_order_relaxed) == current) {
    ++depth_;
    return true;
  }
  if (!os_unfair_lock_trylock(&mutex_)) {
    return false;
  }
  thread_id_.store(current, std::memory_order_relaxed);
  ++depth_;
  return true;
}

// AppleRecursiveMutex.h:72-79, Unlock: only the outermost release clears the
// owner and publishes protected writes through the native unlock.
void AllocatorRecursiveMutex::unlock() {
  if (--depth_ == 0) {
    thread_id_.store(0, std::memory_order_relaxed);
    os_unfair_lock_unlock(&mutex_);
  }
}

// Private/GenericPlatform/GenericPlatformManualResetEvent.h:23-27,123-125:
// generic event selected by ApplePlatformManualResetEvent.h:5-8 starts waiting.
AllocatorManualResetEvent::AllocatorManualResetEvent() = default;

// GenericPlatformManualResetEvent.h:36-40, Reset: only the waiter resets, with
// no concurrent notifier. The guard protects the state transition.
void AllocatorManualResetEvent::reset() {
  std::unique_lock guard(mutex_);
  waiting_ = true;
}

// GenericPlatformManualResetEvent.h:58-63, Wait: predicate handles an earlier
// notification and spurious wakes. WordMutex disables source oversubscription;
// no scheduler worker creation exists in this extracted primitive.
void AllocatorManualResetEvent::wait() {
  std::unique_lock guard(mutex_);
  condition_.wait(guard, [this] { return !waiting_; });
}

// GenericPlatformManualResetEvent.h:112-119, Notify: hold the guard through the
// last event access so the awakened waiter can safely destroy its stack event.
void AllocatorManualResetEvent::notify() {
  std::unique_lock guard(mutex_);
  waiting_ = false;
  condition_.notify_one();
}
} // namespace bw::platform::macos
