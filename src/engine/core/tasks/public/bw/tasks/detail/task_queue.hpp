#ifndef BW_CORE_TASKS_TASK_QUEUE_HPP
#define BW_CORE_TASKS_TASK_QUEUE_HPP

#include "bw/platform/memory/allocator_sync.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Core CP.2 / Performance CONC.2: indexed links retain storage for outstanding
// readers; acquire/CAS publishes payload and counters across native threads.
namespace bw::tasks {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// LockFreeList.h:544-687. Indexed tagged Michael-Scott FIFO; link payload is
// borrowed until pop transfers it to the consumer. Null is the empty marker.
class TaskQueue final {
public:
  TaskQueue();
  ~TaskQueue();
  TaskQueue(const TaskQueue&) = delete;
  TaskQueue& operator=(const TaskQueue&) = delete;
  void push(void* payload);
  [[nodiscard]] void* pop();

private:
  alignas(platform::allocator_cache_line_size) std::atomic<std::uint64_t> head_{0};
  alignas(platform::allocator_cache_line_size) std::atomic<std::uint64_t> tail_{0};
};

// LockFreeList.h:689-838: two source priorities, tagged stall-state handshake.
// The current dependency exposes idle pumping only, so pop does not mark a
// sleeping owner. The named owner is bit zero; high priority is index zero.
class NamedTaskQueue final {
public:
  void push(void* payload, bool high_priority);
  [[nodiscard]] void* pop();

private:
  std::array<TaskQueue, 2> priorities_;
  alignas(platform::allocator_cache_line_size) std::atomic<std::uint64_t> state_{0};
};
} // namespace bw::tasks
#endif
