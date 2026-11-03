#include "bw/tasks/named_thread_dispatch.hpp"

#include "bw/memory/allocator.hpp"
#include "bw/platform/memory/memory_failure.hpp"
#include "task_storage.hpp"

#include <new>

// CP.2 / CONC.2: only game owner changes graph lifecycle. Attach/detach are
// coordinated native-thread startup/shutdown operations, not concurrent topology
// edits. Task values use linear task storage; callback context is borrowed.
namespace bw::tasks {
namespace {
// TLS identifies the current named owner; processing guard prevents reentry;
// each task holds its own callback value.
struct CurrentOwner final {
  NamedThread role{NamedThread::game};
  bool attached{false};
  bool processing{false};
};
thread_local CurrentOwner current;
struct Task final {
  NamedCallback callback;
};
std::size_t index_of(NamedThread role) { return static_cast<std::size_t>(role); }
void check(bool condition, const char* expression) {
  if (!condition)
    platform::memory_check_failed(expression, __FILE__, __LINE__);
}
} // namespace

// Singleton/startup lifecycle: a small process-lifetime facade; first access
// must be on the game owner (the future host establishes the game thread id).
// Queue storage is allocated only at explicit startup, never during allocator
// bootstrap or a local broadcast.
NamedThreadDispatch::NamedThreadDispatch() : game_thread_(std::this_thread::get_id()) {}
NamedThreadDispatch& NamedThreadDispatch::instance() {
  static NamedThreadDispatch dispatch;
  return dispatch;
}
void NamedThreadDispatch::startup() {
  check(std::this_thread::get_id() == game_thread_ && !running_, "game owner starts graph once");
  using Owners = std::array<Owner, 3>;
  owners_ =
      new (memory::allocate(sizeof(Owners), static_cast<std::uint32_t>(alignof(Owners)))) Owners;
  current = {NamedThread::game, true, false};
  (*owners_)[0].attached.store(true, std::memory_order_release);
  running_ = true;
}

// Shutdown requires named threads stopped first. It drains game tasks, then
// destroys empty queues; no queued callback is discarded.
void NamedThreadDispatch::shutdown() {
  check(std::this_thread::get_id() == game_thread_ && running_, "game owner stops graph");
  check(!(*owners_)[1].attached.load(std::memory_order_acquire) &&
            !(*owners_)[2].attached.load(std::memory_order_acquire),
        "render/RHI owners detached before graph shutdown");
  (void)process_until_idle();
  using Owners = std::array<Owner, 3>;
  owners_->~Owners();
  memory::release(owners_);
  owners_ = nullptr;
  current = {};
  running_ = false;
}

// The native thread binds its named TLS identity; ownership is not simulated
// by routing callbacks onto a different native thread.
void NamedThreadDispatch::attach_current_thread(NamedThread role) {
  check(role != NamedThread::game && !current.attached && owners_ != nullptr,
        "native render/RHI thread attaches to initialized graph");
  check(index_of(role) < 3, "valid source named role");
  current = {role, true, false};
  check(!(*owners_)[index_of(role)].attached.exchange(true, std::memory_order_acq_rel),
        "named role has one native owner");
}
void NamedThreadDispatch::detach_current_thread() {
  check(current.attached && current.role != NamedThread::game, "render/RHI owner detaches itself");
  (void)process_until_idle();
  (*owners_)[index_of(current.role)].attached.store(false, std::memory_order_release);
  current = {};
}

// Execute high/normal tasks on owner until queue empty; there is no blocking
// wait or recursive pump in this selected method.
std::size_t NamedThreadDispatch::process_until_idle() {
  check(current.attached && !current.processing, "owner pump cannot reenter");
  current.processing = true;
  auto& queue = (*owners_)[index_of(current.role)].queue;
  std::size_t processed = 0;
  while (auto* task = static_cast<Task*>(queue.pop())) {
    task->callback.function(task->callback.context, current.role);
    // The copied task outlives broadcast's stack value, then releases its
    // storage after actual execution.
    task->~Task();
    release_task_storage(task);
    ++processed;
  }
  current.processing = false;
  return processed;
}

// Copy payload into task-block storage and publish to the target's selected
// priority FIFO.
void NamedThreadDispatch::enqueue(NamedThread target, NamedCallback callback, bool high_priority) {
  check(index_of(target) < 3 && owners_ != nullptr && callback.function != nullptr,
        "valid named task submission");
  auto& owner = (*owners_)[index_of(target)];
  check(owner.attached.load(std::memory_order_acquire), "target owner is attached");
  auto* task = new (allocate_task_storage(sizeof(Task), alignof(Task))) Task{callback};
  owner.queue.push(task, high_priority);
}

// Call game locally before the graph check, then dispatch RHI/render
// high-priority tasks asynchronously. No worker barrier/three-second timeout
// belongs to this path.
void NamedThreadDispatch::broadcast(NamedCallback callback) {
  check(std::this_thread::get_id() == game_thread_ && callback.function != nullptr,
        "broadcast runs on game owner");
  callback.function(callback.context, NamedThread::game);
  if (!running_)
    return;
  for (const auto role : {NamedThread::rhi, NamedThread::rendering}) {
    if ((*owners_)[index_of(role)].attached.load(std::memory_order_acquire)) {
      enqueue(role, callback, true);
    }
  }
}

// Lifecycle observation is game-owner-only here, avoiding a promise of
// concurrent graph startup/shutdown.
bool NamedThreadDispatch::running() const {
  check(std::this_thread::get_id() == game_thread_, "game owner observes graph lifecycle");
  return running_;
}
} // namespace bw::tasks
