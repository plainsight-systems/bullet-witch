// Unit test: the uncontended protocol of bw/concurrency/word_mutex.hpp.
// Contention is covered by the allocator synchronization integration test.
#include "bw/concurrency/word_mutex.hpp"

#include <cstdio>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error{message};
  }
}
} // namespace

int main() {
  try {
    bw::concurrency::WordMutex mutex;
    require(!mutex.is_locked(), "a new mutex is unlocked");
    require(mutex.try_lock() && mutex.is_locked(), "try_lock acquires a free mutex");
    require(!mutex.try_lock(), "try_lock fails while held; the mutex is nonrecursive");
    mutex.unlock();
    require(!mutex.is_locked(), "unlock releases");
    mutex.lock();
    require(mutex.is_locked() && !mutex.try_lock(), "lock acquires a free mutex");
    mutex.unlock();
    require(!mutex.is_locked(), "final release leaves unlocked state");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
  }
}
