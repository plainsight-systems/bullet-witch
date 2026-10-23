// Memory migration: use the domain-owned retained snapshot; clear it on success
// and exception, keeping capacity without resetting any task/coroutine owner.
// R.1 (#rr-raii) and MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md), retrieved in both phases,
// preserve owned storage and injection. Algorithm/order below is unchanged;
// O(N) work with no snapshot allocation after capacity stabilizes.
// E1: task lifecycle and ordered update passes (0x40b4f0/570/4c0).
// Runs callbacks on the thread calling tick; no worker threads or locks.
// All API calls require one owning thread; concurrent access is unsafe.
// ticking_ only detects recursion and provides no synchronization.
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

// Naming design/implementation consultation: cpp-guidelines P.3 (#rp-what)
// selects role-based names. cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) preserves owning values and implicit
// moves; renaming adds no wrapper, allocation or payload copy.

#include "bw/tasks/task_lifecycle_scheduler.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>
namespace bw::tasks {
namespace {
void invoke_hook(const std::function<void()>& fn) {
  if (fn) {
    fn();
  }
}
}  // namespace
TaskId TaskLifecycleScheduler::add(int priority, Hooks hooks) {
  if (next_ == std::numeric_limits<TaskId>::max()) {
    throw std::overflow_error("Task IDs exhausted");
  }
  const auto id = ++next_;
  tasks_.emplace(std::pair{priority, id}, Task{std::move(hooks)});
  return id;
}
TaskLifecycleScheduler::Task& TaskLifecycleScheduler::task(TaskId id) {
  auto found = std::find_if(
      tasks_.begin(), tasks_.end(),
      [id](const auto& entry) { return entry.first.second == id; });
  if (found == tasks_.end()) {
    throw std::out_of_range("Task ID");
  }
  return found->second;
}
void TaskLifecycleScheduler::request(TaskId id, State target) {
  if (target != State::created && target != State::idle &&
      target != State::active) {
    throw std::invalid_argument("Transient task request");
  }
  task(id).requested = target;
}
void TaskLifecycleScheduler::enable(TaskId id, bool enabled) {
  task(id).enabled = enabled;
}
bool TaskLifecycleScheduler::contains(TaskId id) const {
  return std::any_of(tasks_.begin(), tasks_.end(), [id](const auto& entry) {
    return entry.first.second == id;
  });
}
State TaskLifecycleScheduler::state(TaskId id) const {
  auto found = std::find_if(
      tasks_.begin(), tasks_.end(),
      [id](const auto& entry) { return entry.first.second == id; });
  if (found == tasks_.end()) {
    throw std::out_of_range("Task ID");
  }
  return found->second.current;
}
void TaskLifecycleScheduler::tick(bool late_pass) {
  if (ticking_) {
    throw std::logic_error("Recursive task tick");
  }
  ticking_ = true;
  try {
    order_.clear();
    order_.reserve(tasks_.size());
    for (const auto& [key, value] : tasks_) {
      (void)value;
      order_.push_back(key);
    }
    for (const auto& key : order_) {
      auto& task = tasks_.at(key);
      if (task.faulted) {
        continue;
      }
      try {
        // The recovered lifecycle loops until requested/current agree.
        constexpr unsigned transition_budget = 8;
        bool remove = false;
        for (unsigned n = 0; task.current != task.requested && !remove; ++n) {
          if (n == transition_budget) {
            throw std::runtime_error("Task transition cycle");
          }
          if (task.current == State::created) {
            task.current = State::initializing;
            invoke_hook(task.hooks.initialize);
            task.current = State::idle;
          } else if (task.current == State::idle &&
                     task.requested == State::active) {
            task.current = State::activating;
            invoke_hook(task.hooks.activate);
            task.current = State::active;
          } else if (task.current == State::idle) {
            task.current = State::destroying;
            invoke_hook(task.hooks.destroy);
            remove = true;
          } else if (task.current == State::active) {
            task.current = State::deactivating;
            invoke_hook(task.hooks.deactivate);
            task.current = State::idle;
          } else {
            throw std::logic_error("Invalid lifecycle state");
          }
        }
        if (remove) {
          tasks_.erase(key);
        }
      } catch (...) {
        task.faulted = true;
        throw;
      }
    }
    for (const bool late : {false, true}) {
      if (late && !late_pass) {
        break;
      }
      for (const auto& key : order_) {
        auto found = tasks_.find(key);
        if (found == tasks_.end()) {
          continue;
        }
        auto& task = found->second;
        if (!task.enabled || task.faulted || task.current != State::active) {
          continue;
        }
        task.current = State::updating;
        try {
          invoke_hook(late ? task.hooks.late : task.hooks.update);
          task.current = State::active;
        } catch (...) {
          task.faulted = true;
          throw;
        }
      }
    }
    order_.clear();
    ticking_ = false;
  } catch (...) {
    order_.clear();
    ticking_ = false;
    throw;
  }
}
}  // namespace bw::tasks
