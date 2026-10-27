// Shared-VM callback contracts use only in-memory source and native backing.
// Core SL.C.1 confines Lua longjmp; MEM.9 retains fixed slots. Both design and
// implementation consulted. Tests exercise independent continuation ownership,
// not timing sleeps or application-specific mission outcomes.
#include <array>
#include <memory_resource>
#include <stdexcept>
#include <utility>

#include "bw/script/context.hpp"
namespace {
using bw::script::Context;
using bw::script::Reply;
using bw::script::RunState;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template <typename F>
void rejects(F&& operation) {
  bool rejected = false;
  try {
    std::forward<F>(operation)();
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, "Invalid coroutine operation must reject");
}
class Backing final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    try {
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (...) {
      return nullptr;
    }
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
};
void interleaved(const std::shared_ptr<bw::memory::Domain>& domain) {
  Context context(domain);
  context.reserve_coroutines(2);
  std::array<bool, 3> waiting{true, true, true};
  context.bind("wait", [&](auto args) {
    const auto slot = static_cast<std::size_t>(std::get<double>(args.front()));
    return Reply{{}, [&, slot] { return waiting.at(slot); }};
  });
  context.load_source(
      "value=0; function main() wait(0); value=value+100 end "
      "function one() wait(1); value=value+1 end "
      "function two() wait(2); value=value+10 end");
  context.start("main");
  const auto one = context.start_coroutine("one");
  const auto two = context.start_coroutine("two");
  require(context.pump().state == RunState::pending &&
              context.pump(one).state == RunState::pending &&
              context.pump(two).state == RunState::pending,
          "Three independent waits");
  rejects([&] { static_cast<void>(context.start_coroutine("one")); });
  rejects([&] { context.reserve_coroutines(3); });
  rejects([&] { context.load_source("value=999"); });
  waiting[2] = false;
  require(context.pump(two).state == RunState::complete &&
              std::get<double>(context.global("value")) == 10 &&
              context.pump(one).state == RunState::pending,
          "Sibling wait remains private");
  context.retire_coroutine(two);
  rejects([&] { static_cast<void>(context.pump(two)); });
  const auto replacement = context.start_coroutine("two");
  require(replacement != two &&
              context.pump(replacement).state == RunState::complete,
          "Reused storage gets a fresh identity");
  waiting[1] = false;
  waiting[0] = false;
  auto moved = std::move(context);
  require(moved.pump(one).state == RunState::complete &&
              moved.pump().state == RunState::complete &&
              std::get<double>(moved.global("value")) == 121,
          "Moves retain shared globals and independent continuations");
  moved.retire_coroutine(one);
  moved.retire_coroutine(replacement);
  rejects([&] { moved.retire_coroutine(one); });
}
void failure_isolation(const std::shared_ptr<bw::memory::Domain>& domain) {
  Context context(domain);
  context.reserve_coroutines(3);
  context.bind("fail", [](auto) -> Reply {
    throw std::runtime_error("child native failure");
  });
  context.bind("nested", [&](auto) -> Reply {
    static_cast<void>(context.pump());
    return {};
  });
  context.bind("badpoll", [](auto) -> Reply {
    return {{},
            []() -> bool { throw std::runtime_error("child poll failure"); }};
  });
  context.load_source(
      "function main() value=7 end function failed() fail() end "
      "function reentrant() nested() end function pollfailed() badpoll() end");
  context.start("main");
  const auto failed = context.start_coroutine("failed");
  const auto nested = context.start_coroutine("reentrant");
  const auto poll = context.start_coroutine("pollfailed");
  require(context.pump(failed).state == RunState::failed &&
              context.pump(failed).diagnostic.find("child native failure") !=
                  std::string::npos,
          "Child native error remains local and terminal");
  require(context.pump(nested).state == RunState::failed &&
              context.pump(nested).diagnostic.find("Reentrant") !=
                  std::string::npos,
          "Nested pump rejects before changing another task");
  require(context.pump(poll).state == RunState::failed &&
              context.pump(poll).diagnostic == "child poll failure",
          "Poll error is local");
  require(context.pump().state == RunState::complete &&
              std::get<double>(context.global("value")) == 7,
          "Primary survives child errors");
  context.retire_coroutine(failed);
  context.retire_coroutine(nested);
  context.retire_coroutine(poll);
  rejects([&] { static_cast<void>(context.start_coroutine("missing")); });
  const auto recovered = context.start_coroutine("main");
  require(context.pump(recovered).state == RunState::complete,
          "Failed start releases its slot and anchor");
  context.retire_coroutine(recovered);
}
void cancellation_and_reuse(const std::shared_ptr<bw::memory::Domain>& domain) {
  std::weak_ptr<int> retained;
  {
    Context context(domain);
    context.reserve_coroutines(1);
    context.bind("wait", [&](auto) {
      auto owner = std::make_shared<int>(1);
      retained = owner;
      return Reply{{}, [owner] { return *owner > 0; }};
    });
    context.load_source(
        "function run() local keep={1,2,3,4}; wait(); after=keep[1] end");
    const auto first = context.start_coroutine("run");
    require(
        context.pump(first).state == RunState::pending && !retained.expired(),
        "Waiting continuation owns native poll");
    context.retire_coroutine(first);
    require(retained.expired() &&
                std::holds_alternative<std::monostate>(context.global("after")),
            "Cancellation destroys poll without resuming script");
    const auto baseline = context.memory_snapshot().live_bytes;
    for (int iteration = 0; iteration < 100; ++iteration) {
      const auto next = context.start_coroutine("run");
      require(context.pump(next).state == RunState::pending,
              "Bounded callback reuse");
      context.retire_coroutine(next);
      require(context.memory_snapshot().live_bytes == baseline &&
                  retained.expired(),
              "Retirement releases Lua stack and native capture each time");
    }
    const auto last = context.start_coroutine("run");
    require(context.pump(last).state == RunState::pending,
            "Final suspended callback");
  }
  require(retained.expired(), "Context destruction cancels every child");
}
void failed_start_unwinds(const std::shared_ptr<bw::memory::Domain>& domain) {
  Context context(domain);
  context.reserve_coroutines(1);
  context.load_source("function run() value=42 end");
  context.start("run");
  bool exhausted = false;
  try {
    static_cast<void>(
        context.start_coroutine(std::string(bw::script::kRootHeapBytes, 'x')));
  } catch (const std::bad_alloc&) {
    exhausted = true;
  }
  require(exhausted && context.memory_snapshot().rejected_requests != 0,
          "Child allocation failure returns through the protected C boundary");
  require(context.pump().state == RunState::complete,
          "Primary remains resumable after failed child creation");
  const auto replacement = context.start_coroutine("run");
  require(context.pump(replacement).state == RunState::complete,
          "Failed child creation releases its capacity");
  context.retire_coroutine(replacement);
}
}  // namespace
void script_coroutine_tests() {
  const auto domain = bw::memory::Domain::create(std::make_shared<Backing>());
  interleaved(domain);
  failure_isolation(domain);
  cancellation_and_reuse(domain);
  failed_start_unwinds(domain);
  require(domain->snapshot().live_bytes == 0,
          "No retained task storage after VM close");
}
