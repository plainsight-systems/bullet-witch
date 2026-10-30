#ifndef BW_CORE_TASKS_NAMED_THREAD_DISPATCH_HPP
#define BW_CORE_TASKS_NAMED_THREAD_DISPATCH_HPP

#include "bw/tasks/detail/task_queue.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <thread>

// Core CP.2: role lifecycle belongs to the game owner; queued task values own
// callback/context copies, never references to stack callback objects.
// Performance CONC.2: role publication and task queues establish visibility.
namespace bw::tasks {
enum class NamedThread : unsigned char { game, rendering, rhi };

// Restricted callback form for the allocator's capture: function and borrowed
// context are copied into task storage. Context MUST outlive queued execution;
// Binned3 supplies its process-lifetime allocator. No default-heap type erasure.
struct NamedCallback final {
  void (*function)(void* context, NamedThread thread) noexcept;
  void* context;
};

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// TaskGraph.cpp:665-687,850-870,1913-2009. Explicit game-owner startup; actual
// render/RHI native owners attach, pump their queues, drain then detach. Lifecycle
// changes must be coordinated with the game thread (as renderer start/stop is).
// This is the named-thread idle-pump dependency, not the general jobs scheduler.
class NamedThreadDispatch final {
public:
  static NamedThreadDispatch& instance();
  NamedThreadDispatch(const NamedThreadDispatch&) = delete;
  NamedThreadDispatch& operator=(const NamedThreadDispatch&) = delete;
  void startup();
  void shutdown();
  void attach_current_thread(NamedThread role);
  void detach_current_thread();
  [[nodiscard]] std::size_t process_until_idle();
  void enqueue(NamedThread target, NamedCallback callback, bool high_priority = false);
  // Game callback executes immediately even before startup; running named
  // owners receive high-priority tasks. Returns WITHOUT waiting for them.
  void broadcast(NamedCallback callback);
  [[nodiscard]] bool running() const;

private:
  NamedThreadDispatch();
  struct Owner final {
    NamedTaskQueue queue;
    std::atomic<bool> attached{false};
  };
  std::array<Owner, 3>* owners_{nullptr};
  std::thread::id game_thread_;
  bool running_{false};
};
} // namespace bw::tasks
#endif
