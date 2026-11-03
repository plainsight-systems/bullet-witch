#include "bw/concurrency/word_mutex.hpp"

#include "allocator_checks.hpp"
#include "bw/platform/memory/allocator_sync.hpp"

// Core CP.2: atomic state publishes stack nodes; queue ownership guards links.
// Performance CONC.4: retain the bounded spin then sleeping waiters.
namespace bw::concurrency {
namespace {
constexpr std::uintptr_t locked = 1;
constexpr std::uintptr_t queue_locked = 2;
constexpr std::uintptr_t queue_mask = ~(locked | queue_locked);

// Stack-owned event and backwards links; the queue owner fills forward links
// and tail's head pointer.
struct Waiter {
  Waiter* previous{};
  Waiter* next{};
  platform::AllocatorManualResetEvent event;
};
static_assert((alignof(Waiter) & queue_mask) == alignof(Waiter));
} // namespace

// Relaxed observation, not ownership acquisition.
bool WordMutex::is_locked() const { return (state_.load(std::memory_order_relaxed) & locked) != 0; }

// Acquire only an entirely empty/unlocked state.
bool WordMutex::try_lock() {
  std::uintptr_t expected = 0;
  return state_.compare_exchange_strong(expected, locked, std::memory_order_acquire,
                                        std::memory_order_relaxed);
}

// Single weak fast-path CAS, then the contention path.
void WordMutex::lock() {
  std::uintptr_t expected = 0;
  if (!state_.compare_exchange_weak(expected, locked, std::memory_order_acquire,
                                    std::memory_order_relaxed)) {
    lock_slow();
  }
}

// Publish release immediately. A different queue owner already owes a wakeup;
// with no queue there is nothing further to do.
void WordMutex::unlock() {
  const auto prior = state_.fetch_sub(locked, std::memory_order_release);
  BW_ALLOCATOR_CHECK((prior & locked) != 0);
  if ((prior & queue_mask) != 0 && (prior & queue_locked) == 0) {
    unlock_slow(prior);
  }
}

// Unfair acquisition can bypass the queue; bounded spin is followed by a stack
// wait node with no heap allocation.
void WordMutex::lock_slow() {
  unsigned spins = 0;
  for (;;) {
    auto current = state_.load(std::memory_order_relaxed);
    // Acquire even if waiters already exist.
    if ((current & locked) == 0) {
      if (state_.compare_exchange_weak(current, current | locked, std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
        return;
      }
      continue;
    }
    // Forty processor pauses only while there is no queue.
    if ((current & queue_mask) == 0 && spins < 40) {
      platform::allocator_spin_yield();
      ++spins;
      continue;
    }
    // Initialize privately, then publish as the tail. Failed CAS
    // never publishes the node; successful CAS keeps it alive until notification.
    Waiter self;
    self.event.reset();
    // The state packs queue address and flags in the same atomic word.
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
    // No scheduler oversubscription is suppressed while waiting: this leaf starts
    // no scheduler workers. Wake retries acquisition, not handoff.
    self.event.wait();
    spins = 0;
  }
}

// Exclusively update queue links, remove one head and wake it; newcomers can
// still acquire the lock or append.
void WordMutex::unlock_slow(std::uintptr_t current) {
  current &= ~locked;
  // Acquire queue ownership or leave another owner to wake.
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
    // The tagged-word queue requires restoring the published waiter pointer.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    auto* tail = reinterpret_cast<Waiter*>(current & queue_mask);
    // Materialize forward links added since the last wake.
    for (auto* node = tail; tail->next == nullptr;) {
      auto* previous = node->previous;
      BW_ALLOCATOR_CHECK(previous != nullptr);
      tail->next = previous->next;
      previous->next = node;
      node = previous;
    }
    // After a failed state update, an intervening owner may
    // have acquired the lock. Release queue ownership and let that owner wake.
    if ((current & locked) != 0) {
      if (state_.compare_exchange_weak(current, current & ~queue_locked, std::memory_order_release,
                                       std::memory_order_acquire)) {
        return;
      }
      continue;
    }
    auto* head = tail->next;
    // Last-node removal must retry if a new tail appeared;
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
    // Notification may destroy head immediately; never touch it
    // after this call. The native event retains its own guard until access ends.
    head->event.notify();
    return;
  }
}
} // namespace bw::concurrency
