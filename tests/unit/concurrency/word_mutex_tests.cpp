// Unit test: the uncontended protocol of bw/concurrency/word_mutex.hpp.
// Contention is covered by the allocator synchronization integration test.
#include "bw/concurrency/word_mutex.hpp"

#include "bw/testing/harness.hpp"

#include <array>

namespace {
using bw::testing::require;

// The executable's contracts, one CTest entry.
void contracts() {
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
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{Scenario{"word_mutex", contracts}};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
