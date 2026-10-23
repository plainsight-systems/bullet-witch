// V1: public runtime contracts, docs/startup-runtime.md. Failures throw and
// produce nonzero exit; fixtures own their state. Algorithm: drive lifecycle,
// resume waits and check terminal outcomes. Cost bounded by small
// scripts/tasks. Optimization: in-memory fixtures avoid unrelated host I/O. MCP
// design and implementation: I.30 (#ri-encapsulate), SL.con.3 (#rsl-bounds),
// COPY.5 (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// guide owned context lifetime, bounded fixtures and callback boundaries.
// Naming design/implementation consultation: cpp-guidelines P.3 (#rp-what)
// selects role-based names. cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) preserves owning values and implicit
// moves; renaming adds no wrapper, allocation or payload copy.

// Startup composition design/implementation: cpp-guidelines I.1 (#ri-explicit)
// keeps scenario entry points explicit; cpp-performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps flow fixtures
// bounded and setup-owned. Native device allocation is tested separately.

#include <iostream>
#include <stdexcept>
#include <string>

#include "bw/script/chunk.hpp"
#include "bw/script/context.hpp"
#include "bw/tasks/task_lifecycle_scheduler.hpp"
void task_memory_contracts();
void lua_memory_contracts();
void startup_flow_contracts();
void startup_session_contracts();
void advertise_flow_contracts();
void title_flow_contracts();
void pcm_buffer_contracts();
void startup_playback_contracts();
void test_menu_flow();
namespace {
void check(bool ok, const char* message) {
  if (!ok) {
    throw std::runtime_error(message);
  }
}
void bridge_contract() {
  using bw::script::Reply;
  using bw::script::RunState;
  bw::script::Context context;
  int polls = 0;
  std::string retained;
  context.bind("wait", [&](auto args) {
    check(args.size() == 1, "Wait arguments");
    retained = std::get<std::string>(args[0]);
    return Reply{{}, [&] { return ++polls < 3; }};
  });
  context.bind("twice", [](auto args) {
    return Reply{{std::get<double>(args[0]) * 2}, {}};
  });
  context.load_source(
      "function nested() wait('retained') end function run() before=twice(3); "
      "nested(); after=twice(before) end");
  context.start("run");
  check(context.pump().state == RunState::pending, "First wait yields");
  check(
      retained == "retained" && std::get<double>(context.global("before")) == 6,
      "Arguments/results preserved");
  check(std::holds_alternative<std::monostate>(context.global("after")),
        "No premature resume");
  auto moved = std::move(context);
  check(moved.pump().state == RunState::pending,
        "Moved VM retains continuation");
  check(moved.pump().state == RunState::complete,
        "Completion resumes in same pump");
  check(std::get<double>(moved.global("after")) == 12, "Continued Lua result");
  moved.start("run");
  check(moved.pump().state == RunState::complete,
        "Already completed wait resumes immediately");
}
void failures() {
  using bw::script::RunState;
  bw::script::Context context;
  context.bind("fail", [](auto) -> bw::script::Reply {
    throw std::runtime_error("native failure");
  });
  context.load_source("function run() fail() end");
  context.start("run");
  auto failure = context.pump();
  check(failure.state == RunState::failed &&
            failure.diagnostic.find("native failure") != std::string::npos,
        "Native exception translated");
  check(context.pump().state == RunState::failed, "Error remains terminal");
  bw::script::Context missing;
  missing.load_source("function run() unknown_native() end");
  missing.start("run");
  check(missing.pump().state == RunState::failed,
        "Missing binding fails explicitly");
  bw::script::Context loop;
  loop.load_source("function run() while true do end end");
  loop.start("run");
  check(loop.pump().diagnostic.find("budget") != std::string::npos,
        "Instruction guard");
  bool malformed = false;
  try {
    (void)bw::script::host_chunk({});
  } catch (const bw::resources::Error&) {
    malformed = true;
  }
  check(malformed, "Truncated bytecode rejected");
  bw::resources::Bytes bad_header{0x1b, 'L', 'u', 'a', 0x50, 1,   4, 4,
                                  4,    6,   8,   9,   9,    8,   0, 0,
                                  0,    0,   0,   0,   0xf8, 0x7f};
  malformed = false;
  try {
    (void)bw::script::host_chunk(bad_header);
  } catch (const bw::resources::Error&) {
    malformed = true;
  }
  check(malformed, "Nonfinite bytecode ABI sentinel rejected");
}
void result_stack_and_wait_error() {
  using bw::script::Reply;
  using bw::script::RunState;
  bw::script::Context many;
  many.bind("many", [](auto) {
    return Reply{std::vector<bw::script::Value>(100, 3.0), {}};
  });
  many.load_source(
      "function run() local values={many()}; last=values[100] end");
  many.start("run");
  check(many.pump().state == RunState::complete &&
            std::get<double>(many.global("last")) == 3,
        "Native result stack grows safely");
  bw::script::Context waiting;
  waiting.bind("wait", [](auto) {
    return Reply{{}, []() -> bool { throw std::runtime_error("poll failed"); }};
  });
  waiting.load_source("function run() wait() end");
  waiting.start("run");
  const auto result = waiting.pump();
  check(result.state == RunState::failed && result.diagnostic == "poll failed",
        "Pending callback errors are terminal");
}
void scheduling() {
  using bw::tasks::Hooks;
  using bw::tasks::State;
  bw::tasks::TaskLifecycleScheduler scheduler;
  std::string trace;
  auto id =
      scheduler.add(30, Hooks{[&] { trace += 'i'; }, [&] { trace += 'a'; },
                              [&] { trace += 'd'; }, [&] { trace += 'x'; },
                              [&] { trace += 'u'; }, [&] { trace += 'l'; }});
  scheduler.tick();
  check(trace == "iaul", "Lifecycle settles before both passes");
  scheduler.enable(id, false);
  scheduler.tick();
  check(trace == "iaul", "Disabled task does not update");
  scheduler.request(id, State::idle);
  scheduler.tick();
  check(trace == "iauld" && scheduler.state(id) == State::idle,
        "Deactivation reaches idle");
  scheduler.request(id, State::active);
  scheduler.enable(id, true);
  scheduler.tick(false);
  check(trace == "iauldau", "Late pass is gated");
  scheduler.request(id, State::created);
  scheduler.tick();
  check(trace == "iauldaudx" && !scheduler.contains(id),
        "Stop deactivates then destroys");
  trace.clear();
  (void)scheduler.add(30, Hooks{.update = [&] { trace += 'b'; },
                                .late = [&] { trace += 'B'; }});
  (void)scheduler.add(20, Hooks{.update = [&] { trace += 'a'; },
                                .late = [&] { trace += 'A'; }});
  scheduler.tick();
  check(trace == "abAB", "Priority-ordered separate passes");
}
}  // namespace
int main() {
  try {
    startup_flow_contracts();
    startup_session_contracts();
    advertise_flow_contracts();
    title_flow_contracts();
    pcm_buffer_contracts();
    startup_playback_contracts();
    test_menu_flow();
    bridge_contract();
    failures();
    scheduling();
    task_memory_contracts();
    lua_memory_contracts();
    result_stack_and_wait_error();
    std::cout << "Runtime contract cases passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
