// Memory migration: task nodes and the nonescaping priority snapshot retain an
// injected domain. The snapshot clears after each tick, retains capacity, and
// never owns suspended callbacks. Legacy default construction is tests/tools
// only; std::function internals remain an explicit library allocation boundary.
// Allocation failure unwinds without fallback; the scheduler remains usable.
// Design/implementation MCP R.1 (#rr-raii) owns cleanup and MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md)
// propagates the capability. Cost: O(N) snapshot; capacity grows only with
// high-water demand. See
// research/engine/subsystems/memory-migration-completion.md. E1: task lifecycle
// and ordered update passes (0x40b4f0/570/4c0). Runs callbacks on the thread
// calling tick; no worker threads or locks. All API calls require one owning
// thread; concurrent access is unsafe. ticking_ only detects recursion and
// provides no synchronization. Owns callbacks, single-threaded; callbacks may
// request transitions/add tasks, but may not recursively tick. IDs remain
// stable; new tasks join next tick. Callback exceptions mark a task faulted and
// propagate; it never updates again. Algorithm: snapshot priority order, settle
// lifecycle, update eligible active tasks, then optional late pass. Destroy
// removes after callback. O(N log N) registration, O(N) tick snapshot.
// Optimization: callbacks retain their state. Design/implementation MCP: I.30
// (#ri-encapsulate) confines C/runtime details; SL.con.3 (#rsl-bounds) checks
// byte/index access. cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md):
// unique runtime owners cannot be copied; moves preserve stable heap identity.

// Naming design/implementation consultation: cpp-guidelines P.3 (#rp-what)
// selects role-based names. cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) preserves owning values and implicit
// moves; renaming adds no wrapper, allocation or payload copy.

#pragma once
#include <cstdint>
#include <functional>
#include <map>

#include "bw/memory/allocator.hpp"
namespace bw::tasks {
using TaskId = std::uint64_t;
enum class State : std::uint8_t {
  created = 1,
  initializing = 2,
  idle = 4,
  activating = 8,
  active = 16,
  updating = 32,
  deactivating = 64,
  destroying = 128
};
struct Hooks {
  std::function<void()> initialize, activate, deactivate, destroy, update, late;
};
class TaskLifecycleScheduler {
 public:
  TaskLifecycleScheduler() = default;
  explicit TaskLifecycleScheduler(std::shared_ptr<memory::Domain> domain)
      : tasks_(memory::Allocator<std::pair<const Key, Task>>(domain)),
        order_(memory::Allocator<Key>(std::move(domain))) {}
  TaskId add(int priority, Hooks hooks);
  void request(TaskId id, State target);
  void enable(TaskId id, bool enabled);
  State state(TaskId id) const;
  bool contains(TaskId id) const;
  void tick(bool late_pass = true);

 private:
  using Key = std::pair<int, TaskId>;
  struct Task {
    Hooks hooks;
    State current{State::created}, requested{State::active};
    bool enabled{true}, faulted{};
  };
  Task& task(TaskId id);
  std::map<Key, Task, std::less<>,
           memory::Allocator<std::pair<const Key, Task>>>
      tasks_;
  memory::Vector<Key> order_;
  TaskId next_{};
  bool ticking_{};
};
}  // namespace bw::tasks
