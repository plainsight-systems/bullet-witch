// Integration test: public contracts of bw/tasks/named_thread_dispatch.hpp.
// Needs threads.
#include "bw/tasks/named_thread_dispatch.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <semaphore>
#include <string>
#include <string_view>
#include <thread>

namespace {
using bw::testing::require;

// The scenario's name, shared by the table and the worker-thread helper.
constexpr std::string_view kScenario = "named_thread_dispatch";

// A requirement checked on a worker thread, where a throw cannot reach the
// harness: it prints "<scenario>: <message>" and ends the process with 1.
// The line goes out in one write so failing threads do not interleave.
void require_on_thread(bool condition, std::string_view message) {
  if (!condition) {
    std::string line(kScenario);
    line.append(": ").append(message).append("\n");
    std::fwrite(line.data(), 1, line.size(), stderr);
    std::_Exit(1);
  }
}

struct Observation final {
  std::array<unsigned, 3> calls{};
};

void observe(void* context, bw::tasks::NamedThread role) noexcept {
  auto& observed = *static_cast<Observation*>(context);
  ++observed.calls[static_cast<std::size_t>(role)];
}

void broadcast_ownership() {
  auto& dispatch = bw::tasks::NamedThreadDispatch::instance();
  Observation observed;
  dispatch.broadcast({observe, &observed});
  require(observed.calls[0] == 1 && observed.calls[1] == 0 && observed.calls[2] == 0,
          "before startup only the game callback executes");
  dispatch.startup();
  require(dispatch.running(), "startup makes the dispatch running");
  std::counting_semaphore<2> attached{0};
  std::counting_semaphore<2> pump{0};
  std::counting_semaphore<2> completed{0};
  const auto owner = [&](bw::tasks::NamedThread role) {
    dispatch.attach_current_thread(role);
    attached.release();
    pump.acquire();
    require_on_thread(dispatch.process_until_idle() == 1,
                      "an owner receives exactly one broadcast");
    completed.release();
    dispatch.detach_current_thread();
  };
  std::thread renderer(owner, bw::tasks::NamedThread::rendering);
  std::thread rhi(owner, bw::tasks::NamedThread::rhi);
  attached.acquire();
  attached.acquire();
  dispatch.broadcast({observe, &observed});
  require(observed.calls[0] == 2 && observed.calls[1] == 0 && observed.calls[2] == 0,
          "broadcast does not run another owner's callback or wait for it");
  pump.release(2);
  completed.acquire();
  completed.acquire();
  require(observed.calls == std::array<unsigned, 3>{2, 1, 1},
          "each attached owner ran the broadcast once");
  renderer.join();
  rhi.join();
  dispatch.shutdown();
  require(!dispatch.running(), "shutdown stops the dispatch");
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
  require(order.values == std::array<unsigned, 4>{2, 3, 1, 4}, "high priority first, then FIFO");
  require(dispatch.process_until_idle() == 0, "an idle pump returns zero");
  dispatch.shutdown();
}

// The executable's contracts, one CTest entry.
void contracts() {
  broadcast_ownership();
  priorities_and_fifo();
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{Scenario{kScenario, contracts}};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
