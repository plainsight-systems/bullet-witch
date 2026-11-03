#ifndef BW_CORE_CONCURRENCY_WORD_MUTEX_HPP
#define BW_CORE_CONCURRENCY_WORD_MUTEX_HPP

#include <atomic>
#include <cstdint>

// Core CP.2: lock state and waiter ownership use one atomic protocol.
// Core I.22/C.48: constant initialization and shared zero member state;
// Performance LIFE.6's first-use singleton alternative is not selected.
namespace bw::concurrency {
// Nonrecursive and unfair; contention queues stack waiters, without heap nodes.
class WordMutex final {
public:
  // First-declaration constexpr defaulting uses the atomic zero member
  // initializer before other units' dynamic startup.
  constexpr WordMutex() = default;
  WordMutex(const WordMutex&) = delete;
  WordMutex& operator=(const WordMutex&) = delete;
  [[nodiscard]] bool is_locked() const;
  [[nodiscard]] bool try_lock();
  void lock();
  void unlock();

private:
  void lock_slow();
  void unlock_slow(std::uintptr_t prior_state);
  std::atomic<std::uintptr_t> state_{0};
};
} // namespace bw::concurrency

#endif
