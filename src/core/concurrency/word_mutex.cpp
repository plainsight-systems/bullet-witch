#include "core/concurrency/word_mutex.hpp"

#include "core/memory/allocator_checks.hpp"
#include "platform/allocator_sync.hpp"

// Core CP.2: atomic state publishes stack nodes; queue ownership guards links.
// Performance CONC.4: retain the source's bounded spin then sleeping waiters.
// Unreal reference: f45729c0b6f097038e72b52d366a62baeca3275b.
namespace bw::concurrency {
namespace {
constexpr std::uintptr_t locked = 1;
constexpr std::uintptr_t queue_locked = 2;
constexpr std::uintptr_t queue_mask = ~(locked | queue_locked);

// Private/Async/WordMutex.cpp:12-21, FWordMutexQueueNode: stack-owned event and
// backwards links; the queue owner fills forward links and tail's head pointer.
struct Waiter {
  Waiter* previous{};
  Waiter* next{};
  platform::AllocatorManualResetEvent event;
};
static_assert((alignof(Waiter) & queue_mask) == alignof(Waiter));
} // namespace

// Public/Async/WordMutex.h:23,76: zero initializes the unlocked, empty state.
WordMutex::WordMutex() = default;

// WordMutex.h:28-31, IsLocked: relaxed observation, not ownership acquisition.
bool WordMutex::is_locked() const { return (state_.load(std::memory_order_relaxed) & locked) != 0; }

// WordMutex.h:33-37, TryLock: acquire only an entirely empty/unlocked state.
bool WordMutex::try_lock() {
  std::uintptr_t expected = 0;
  return state_.compare_exchange_strong(expected, locked, std::memory_order_acquire,
                                        std::memory_order_relaxed);
}

// WordMutex.h:39-48, Lock: single weak fast-path CAS, then source contention path.
void WordMutex::lock() {
  std::uintptr_t expected = 0;
  if (!state_.compare_exchange_weak(expected, locked, std::memory_order_acquire,
                                    std::memory_order_relaxed)) {
    lock_slow();
  }
}

// WordMutex.h:50-67, Unlock: publish release immediately. A different queue owner
// already owes a wakeup; with no queue there is nothing further to do.
void WordMutex::unlock() {
  const auto prior = state_.fetch_sub(locked, std::memory_order_release);
  BW_ALLOCATOR_CHECK((prior & locked) != 0);
  if ((prior & queue_mask) != 0 && (prior & queue_locked) == 0) {
    unlock_slow(prior);
  }
}

// Private/Async/WordMutex.cpp:23-84, LockSlow: unfair acquisition can bypass the
// queue; bounded spin is followed by a stack wait node with no heap allocation.
void WordMutex::lock_slow() {
  unsigned spins = 0;
  for (;;) {
    auto current = state_.load(std::memory_order_relaxed);
    // LockSlow:34-42: acquire even if waiters already exist.
    if ((current & locked) == 0) {
      if (state_.compare_exchange_weak(current, current | locked, std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
        return;
      }
      continue;
    }
    // LockSlow:45-51: forty processor pauses only while there is no queue.
    if ((current & queue_mask) == 0 && spins < 40) {
      platform::allocator_spin_yield();
      ++spins;
      continue;
    }
    // LockSlow:54-72: initialize privately, then publish as the tail. Failed CAS
    // never publishes the node; successful CAS keeps it alive until notification.
    Waiter self;
    self.event.reset();
    // Source packs queue address and flags in the same atomic word.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    if (auto* tail = reinterpret_cast<Waiter*>(current & queue_mask)) {
      self.previous = tail;
    } else {
      self.next = &self;
    }
    const auto queued = (current & ~queue_mask) | reinterpret_cast<std::uintptr_t>(&self);
    if (!state_.compare_exchange_weak(current, queued, std::memory_order_acq_rel,
                                      std::memory_order_relaxed)) {
      continue;
    }
    // LockSlow:75-83: source suppresses scheduler oversubscription while waiting.
    // This leaf starts no scheduler workers. Wake retries acquisition, not handoff.
    self.event.wait();
    spins = 0;
  }
}

// Private/Async/WordMutex.cpp:86-164, UnlockSlow: exclusively update queue links,
// remove one head and wake it; newcomers can still acquire the lock or append.
void WordMutex::unlock_slow(std::uintptr_t current) {
  current &= ~locked;
  // UnlockSlow:91-106: acquire queue ownership or leave another owner to wake.
  for (;;) {
    if (state_.compare_exchange_weak(current, current | queue_locked, std::memory_order_acquire,
                                     std::memory_order_relaxed)) {
      current |= queue_locked;
      break;
    }
    if ((current & queue_locked) != 0 || (current & queue_mask) == 0) {
      return;
    }
  }
  for (;;) {
    // Source tagged-word queue requires restoring the published waiter pointer.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* tail = reinterpret_cast<Waiter*>(current & queue_mask);
    // UnlockSlow:113-123: materialize forward links added since the last wake.
    for (auto* node = tail; tail->next == nullptr;) {
      auto* previous = node->previous;
      BW_ALLOCATOR_CHECK(previous != nullptr);
      tail->next = previous->next;
      previous->next = node;
      node = previous;
    }
    // UnlockSlow:125-136: after a failed state update, an intervening owner may
    // have acquired the lock. Release queue ownership and let that owner wake.
    if ((current & locked) != 0) {
      if (state_.compare_exchange_weak(current, current & ~queue_locked, std::memory_order_release,
                                       std::memory_order_acquire)) {
        return;
      }
      continue;
    }
    auto* head = tail->next;
    // UnlockSlow:138-158: last-node removal must retry if a new tail appeared;
    // otherwise detach only the head and preserve concurrently appended tails.
    if (auto* next_head = head->next; next_head == head) {
      if (!state_.compare_exchange_strong(current, current & locked, std::memory_order_release,
                                          std::memory_order_acquire)) {
        continue;
      }
    } else {
      BW_ALLOCATOR_CHECK(next_head != nullptr);
      next_head->previous = nullptr;
      tail->next = next_head;
      state_.fetch_and(~queue_locked, std::memory_order_release);
    }
    // UnlockSlow:161: notification may destroy head immediately; never touch it
    // after this call. The native event retains its own guard until access ends.
    head->event.notify();
    return;
  }
}
} // namespace bw::concurrency
