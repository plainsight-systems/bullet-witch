// Integration test: allocator synchronization primitives under real threads.
#include "bw/concurrency/word_mutex.hpp"
#include "bw/platform/memory/allocator_sync.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

namespace {
using bw::testing::require;

void recursive_owner() {
  bw::platform::AllocatorRecursiveMutex mutex;
  mutex.lock();
  require(mutex.try_lock(), "recursive owner can acquire again");
  mutex.unlock();
  bool acquired = false;
  std::thread competitor([&] {
    acquired = mutex.try_lock();
    if (acquired) {
      mutex.unlock();
    }
  });
  competitor.join();
  require(!acquired, "inner release preserves owner");
  mutex.unlock();
  competitor = std::thread([&] {
    acquired = mutex.try_lock();
    if (acquired) {
      mutex.unlock();
    }
  });
  competitor.join();
  require(acquired, "outer release permits another owner");
}

// A stack event can disappear immediately after wait completes. The notifier
// deliberately remains in another thread until notify() returns.
void stack_event_lifetime() {
  for (unsigned cycle = 0; cycle < 200; ++cycle) {
    std::thread notifier;
    {
      bw::platform::AllocatorManualResetEvent event;
      event.notify();
      event.wait();
      event.reset();
      notifier = std::thread([&] { event.notify(); });
      event.wait();
    }
    notifier.join();
  }
}

void queued_wait_and_contention() {
  bw::concurrency::WordMutex mutex;
  mutex.lock();
  std::atomic<unsigned> started{0};
  std::atomic<unsigned> acquired{0};
  unsigned counter = 0;
  std::array<std::thread, 8> workers;
  for (auto& worker : workers) {
    worker = std::thread([&] {
      started.fetch_add(1);
      for (unsigned iteration = 0; iteration < 10000; ++iteration) {
        mutex.lock();
        ++counter;
        acquired.fetch_add(1, std::memory_order_relaxed);
        mutex.unlock();
      }
    });
  }
  while (started.load() != workers.size()) {
    std::this_thread::yield();
  }
  // Hold through the bounded CPU spin so contenders exercise sleeping queues.
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const bool stayed_blocked = acquired.load() == 0;
  mutex.unlock();
  for (auto& worker : workers) {
    worker.join();
  }
  require(stayed_blocked, "contenders cannot pass held lock");
  require(counter == 80000 && acquired.load() == counter, "all updates are protected");
}

// The executable's contracts, one CTest entry.
void contracts() {
  recursive_owner();
  stack_event_lifetime();
  queued_wait_and_contention();
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{Scenario{"allocator_sync", contracts}};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
