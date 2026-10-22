// E1: task lifecycle and ordered update passes (0x40b4f0/570/4c0).
// Owns callbacks, single-threaded; callbacks may request transitions/add tasks,
// but may not recursively tick. IDs remain stable; new tasks join next tick.
// Callback exceptions mark a task faulted and propagate; it never updates
// again. Algorithm: snapshot priority order, settle lifecycle, update eligible
// active tasks, then optional late pass. Destroy removes after callback. O(N
// log N) registration, O(N) tick snapshot. Optimization: callbacks retain their
// state. Design/implementation MCP: I.30 (#ri-encapsulate) confines C/runtime
// details; SL.con.3 (#rsl-bounds) checks byte/index access. cpp-performance
// COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md):
// unique runtime owners cannot be copied; moves preserve stable heap identity.

#pragma once
#include <cstdint>
#include <functional>
#include <map>
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
class Scheduler {
 public:
  TaskId add(int priority, Hooks hooks);
  void request(TaskId id, State target);
  void enable(TaskId id, bool enabled);
  State state(TaskId id) const;
  bool contains(TaskId id) const;
  void tick(bool late_pass = true);

 private:
  struct Task {
    Hooks hooks;
    State current{State::created}, requested{State::active};
    bool enabled{true}, faulted{};
  };
  Task& task(TaskId id);
  std::map<std::pair<int, TaskId>, Task> tasks_;
  TaskId next_{};
  bool ticking_{};
};
}  // namespace bw::tasks
