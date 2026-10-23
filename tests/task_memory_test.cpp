// Task allocator contracts exercise retained snapshot storage, admission
// failure and exception recovery through public scheduling behavior. Small
// in-process fixtures have no platform/game dependency. R.1 (#rr-raii) retains
// backing; E.6 (#re-raii) unwinds failures; MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) injects rejection without a
// fallback. Rules retrieved for design and implementation. Cost O(task count).
#include <memory_resource>
#include <stdexcept>

#include "bw/tasks/task_lifecycle_scheduler.hpp"

namespace {
class TaskBacking final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    if (reject) {
      return nullptr;
    }
    try {
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
  bool reject{};
};
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
}  // namespace

void task_memory_contracts() {
  const auto backing = std::make_shared<TaskBacking>();
  auto domain = bw::memory::Domain::create(backing);
  const std::weak_ptr<bw::memory::Domain> lifetime = domain;
  {
    bw::tasks::TaskLifecycleScheduler scheduler(domain);
    unsigned updates = 0;
    (void)scheduler.add(1, {.update = [&] { ++updates; }});
    backing->reject = true;
    bool rejected = false;
    try {
      scheduler.tick();
    } catch (const std::bad_alloc&) {
      rejected = true;
    }
    require(rejected && updates == 0,
            "Snapshot failure invokes no callback and has no fallback");
    backing->reject = false;
    scheduler.tick();
    require(updates == 1, "Snapshot admission failure leaves scheduler usable");
    backing->reject = true;
    scheduler.tick();
    require(updates == 2, "Stable task snapshot reuses owned capacity");
    backing->reject = false;
    (void)scheduler.add(0,
                        {.update = [] { throw std::runtime_error("hook"); }});
    bool callback_failed = false;
    try {
      scheduler.tick();
    } catch (const std::runtime_error&) {
      callback_failed = true;
    }
    scheduler.tick();
    require(callback_failed && updates == 3,
            "Faulted callback does not strand the retained snapshot");
    domain.reset();
    require(!lifetime.expired(), "Scheduler retains allocation capability");
  }
  require(lifetime.expired(), "Scheduler teardown releases its final owner");
}
