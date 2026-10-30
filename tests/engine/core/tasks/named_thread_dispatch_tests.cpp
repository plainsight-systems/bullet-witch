#include "bw/tasks/named_thread_dispatch.hpp"
#include "task_storage.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <semaphore>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {
void require(bool value, const char* message) {
  if (!value) {
    std::fprintf(stderr, "%s\n", message);
    std::abort();
  }
}
struct Observation final {
  std::array<std::thread::id, 3> threads;
  std::array<unsigned, 3> calls{};
};
void observe(void* context, bw::tasks::NamedThread role) noexcept {
  auto& observed = *static_cast<Observation*>(context);
  const auto index = static_cast<std::size_t>(role);
  observed.threads[index] = std::this_thread::get_id();
  ++observed.calls[index];
}
void broadcast_ownership() {
  auto& dispatch = bw::tasks::NamedThreadDispatch::instance();
  Observation observed;
  dispatch.broadcast({observe, &observed});
  require(observed.calls[0] == 1 && observed.calls[1] == 0 && observed.calls[2] == 0,
          "before startup only game callback executes");
  dispatch.startup();
  std::counting_semaphore<2> attached{0};
  std::counting_semaphore<2> pump{0};
  std::counting_semaphore<2> completed{0};
  const auto owner = [&](bw::tasks::NamedThread role) {
    dispatch.attach_current_thread(role);
    attached.release();
    pump.acquire();
    require(dispatch.process_until_idle() == 1, "owner receives exactly one broadcast");
    completed.release();
    dispatch.detach_current_thread();
  };
  std::thread renderer(owner, bw::tasks::NamedThread::rendering);
  std::thread rhi(owner, bw::tasks::NamedThread::rhi);
  attached.acquire();
  attached.acquire();
  dispatch.broadcast({observe, &observed});
  require(observed.calls[0] == 2 && observed.calls[1] == 0 && observed.calls[2] == 0,
          "broadcast must not execute another owner's callback or wait for pump");
  pump.release(2);
  completed.acquire();
  completed.acquire();
  require(observed.threads[0] == std::this_thread::get_id(), "game executes locally");
  require(observed.threads[1] == renderer.get_id() && observed.threads[2] == rhi.get_id(),
          "callbacks execute on actual distinct native owners");
  renderer.join();
  rhi.join();
  dispatch.shutdown();
}
struct Order final {
  std::array<unsigned, 4> values{};
  unsigned count{};
};
struct Item final {
  Order* order;
  unsigned value;
};
void record(void* context, bw::tasks::NamedThread) noexcept {
  const auto& item = *static_cast<Item*>(context);
  item.order->values[item.order->count++] = item.value;
}
void priorities_and_fifo() {
  auto& dispatch = bw::tasks::NamedThreadDispatch::instance();
  dispatch.startup();
  Order order;
  std::array<Item, 4> items{{{&order, 1}, {&order, 2}, {&order, 3}, {&order, 4}}};
  dispatch.enqueue(bw::tasks::NamedThread::game, {record, &items[0]});
  dispatch.enqueue(bw::tasks::NamedThread::game, {record, &items[1]}, true);
  dispatch.enqueue(bw::tasks::NamedThread::game, {record, &items[2]}, true);
  dispatch.enqueue(bw::tasks::NamedThread::game, {record, &items[3]});
  require(dispatch.process_until_idle() == 4, "drain all pending tasks");
  require(order.values == std::array<unsigned, 4>{2, 3, 1, 4}, "priority then FIFO");
  require(dispatch.process_until_idle() == 0, "empty idle pump returns");
  dispatch.shutdown();
}
void concurrent_queue() {
  bw::tasks::TaskQueue queue;
  constexpr std::size_t producers = 4;
  constexpr std::size_t per_producer = 20000;
  static std::array<std::array<unsigned, per_producer>, producers> values;
  std::array<std::thread, producers> workers;
  for (std::size_t producer = 0; producer < producers; ++producer) {
    workers[producer] = std::thread([&, producer] {
      for (std::size_t index = 0; index < per_producer; ++index) {
        values[producer][index] = static_cast<unsigned>(producer * per_producer + index);
        queue.push(&values[producer][index]);
      }
    });
  }
  std::array<std::size_t, producers> consumed{};
  std::size_t total = 0;
  while (total < producers * per_producer) {
    if (auto* value = static_cast<unsigned*>(queue.pop())) {
      const std::size_t producer = *value / per_producer;
      require(*value % per_producer == consumed[producer]++, "producer order/publication");
      ++total;
    } else {
      std::this_thread::yield();
    }
  }
  for (auto& worker : workers)
    worker.join();
  require(queue.pop() == nullptr, "all concurrent queue items consumed exactly once");
}
// Native child keeps the source fatal path observable and bounded. An impossible
// allocation must terminate, not cycle forever trying/reusing the same block.
void oversized_task_fails() {
  const pid_t child = fork();
  require(child >= 0, "create isolated invalid-task case");
  if (child == 0) {
    (void)bw::tasks::allocate_task_storage(65536, 65536);
    _exit(99);
  }
  int status = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    const auto result = waitpid(child, &status, WNOHANG);
    if (result == child)
      break;
    require(result == 0, "wait for invalid-task child");
    if (std::chrono::steady_clock::now() >= deadline) {
      (void)kill(child, SIGKILL);
      (void)waitpid(child, &status, 0);
      require(false, "invalid task size/alignment must fail without looping");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  require(WIFEXITED(status) && WEXITSTATUS(status) == 3,
          "oversized task uses source fatal assertion path");
}
void cross_thread_task_storage() {
  constexpr std::size_t count = 2048;
  std::array<void*, count> pointers{};
  std::thread producer([&] {
    for (std::size_t index = 0; index < count; ++index) {
      pointers[index] = bw::tasks::allocate_task_storage(137, 32);
      require(reinterpret_cast<std::uintptr_t>(pointers[index]) % 32 == 0,
              "task storage alignment");
      std::memset(pointers[index], static_cast<int>(index % 251), 137);
    }
  });
  producer.join();
  for (std::size_t index = 0; index < count; ++index) {
    const auto* bytes = static_cast<const unsigned char*>(pointers[index]);
    require(bytes[0] == index % 251 && bytes[136] == index % 251,
            "task storage survives allocating thread exit");
    bw::tasks::release_task_storage(pointers[index]);
  }
}
} // namespace
int main() {
  oversized_task_fails();
  broadcast_ownership();
  priorities_and_fifo();
  concurrent_queue();
  cross_thread_task_storage();
  std::puts("named-thread dispatch ownership, priority, concurrent FIFO and task storage pass");
}
