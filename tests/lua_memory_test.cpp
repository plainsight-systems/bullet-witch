// Public bounded heap and Lua-context ownership tests. Uses in-memory native
// backing only, not game files/subprocesses. Checks split/coalesce/realloc
// failure, constructor unwind, independent concurrent contexts, yield retention
// and Lua OOM recovery. Capacity is a fixture/input, not a historical claim.
// Design R.1 (#rr-raii), SL.C.1 (#rclib-jmp), performance EMB.4 bounded
// storage; implementation F.6 (#rf-noexcept), MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md): injected fixtures and no
// allocator fallback. See lua-memory-implementation.md.
// Concurrency revision: design CP.3 (#rconc-data), implementation CP.2
// (#rconc-races) keep one exclusive Context/heap per worker; only Domain is
// shared. Both phases retrieve MEM.6 (source above) for that injected boundary.
// This tests independent heaps, not concurrent access to a single heap.
#include <algorithm>
#include <array>
#include <exception>
#include <iostream>
#include <new>
#include <span>
#include <thread>

#include "bw/memory/coalescing_heap.hpp"
#include "bw/script/context.hpp"

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
class Backing final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    // In-memory native test boundary; exact tuple is returned by Domain.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    return ::operator new(bytes, std::align_val_t(alignment), std::nothrow);
  }
  void deallocate(void* pointer, std::size_t,
                  std::size_t alignment) noexcept override {
    ::operator delete(pointer, std::align_val_t(alignment));
  }
};
void coalescing_contract() {
  auto domain = bw::memory::Domain::create(std::make_shared<Backing>());
  {
    bw::memory::CoalescingHeap heap(domain, 4096);
    const auto initial = heap.snapshot();
    auto* first = heap.allocate(200);
    auto* second = heap.allocate(300);
    auto* third = heap.allocate(400);
    require(first && second && third, "bounded allocation");
    require(heap.release(second), "release middle block");
    auto* reused = heap.allocate(128);
    require(reused == second, "first-fit reuses first suitable free region");
    require(heap.release(first) && heap.release(reused) && heap.release(third),
            "free surrounding blocks");
    require(heap.snapshot().largest_free_bytes == initial.largest_free_bytes &&
                heap.snapshot().live_bytes == 0,
            "both adjacent free regions coalesce into full backing");
    auto* retained = heap.allocate(64);
    std::span bytes(static_cast<std::byte*>(retained), 64);
    std::ranges::fill(bytes, std::byte{0x5a});
    require(heap.reallocate(retained, 4096) == nullptr &&
                heap.snapshot().live_bytes == 64 &&
                std::ranges::all_of(
                    bytes, [](auto byte) { return byte == std::byte{0x5a}; }),
            "failed replacement retains old storage and bytes");
    auto* grown = heap.reallocate(retained, 128);
    require(grown && grown != retained &&
                std::ranges::all_of(
                    std::span(static_cast<std::byte*>(grown), 64),
                    [](auto byte) { return byte == std::byte{0x5a}; }),
            "successful replacement copies old bytes before freeing");
    require(heap.reallocate(grown, 0) == nullptr &&
                heap.snapshot().live_allocations == 0,
            "zero realloc releases");
    int foreign = 0;
    require(!heap.release(&foreign),
            "foreign pointer rejected without mutation");
  }
  require(domain->snapshot().live_bytes == 0,
          "heap releases native-backed region");
}
void lua_failure_and_wait_contract() {
  auto domain = bw::memory::Domain::create(std::make_shared<Backing>());
  for (const std::size_t capacity : {128U, 1024U}) {
    bool rejected = false;
    try {
      bw::script::Context tiny(domain, capacity);
    } catch (const std::bad_alloc&) {
      rejected = true;
    }
    require(rejected && domain->snapshot().live_bytes == 0,
            "partial Lua bootstrap failure frees state and heap");
  }
  {
    bw::script::Context context(domain);
    int polls = 0;
    context.bind("wait", [&polls](auto) {
      return bw::script::Reply{{}, [&polls] { return ++polls < 2; }};
    });
    context.load_source("function run() local t={42}; wait(); value=t[1] end");
    context.start("run");
    require(context.pump().state == bw::script::RunState::pending &&
                context.memory_snapshot().live_bytes != 0,
            "pending coroutine retains bounded state");
    require(context.pump().state == bw::script::RunState::complete &&
                std::get<double>(context.global("value")) == 42,
            "yield resumes with retained object values");
    context.load_source(
        "function exhaust() local t={} for i=1,100000 do t[i]={i} end end");
    context.start("exhaust");
    const auto failure = context.pump();
    require(failure.state == bw::script::RunState::failed &&
                context.memory_snapshot().rejected_requests != 0,
            "Lua exhaustion returns protected terminal failure");
  }
  require(domain->snapshot().live_bytes == 0,
          "failed coroutine closes before heap teardown");
}
void protected_host_exhaustion() {
  auto domain = bw::memory::Domain::create(std::make_shared<Backing>());
  const std::string oversized_name(bw::script::kRootHeapBytes, 'x');
  for (unsigned operation = 0; operation < 3; ++operation) {
    {
      bw::script::Context context(domain);
      context.load_source("function run() end");
      bool rejected = false;
      try {
        if (operation == 0) {
          context.bind(oversized_name,
                       [](auto) { return bw::script::Reply{}; });
        } else if (operation == 1) {
          context.start(oversized_name);
        } else {
          (void)context.global(oversized_name);
        }
      } catch (const std::bad_alloc&) {
        rejected = true;
      }
      require(rejected && context.memory_snapshot().rejected_requests != 0,
              "allocating host operation returns protected Lua memory error");
    }
    require(domain->snapshot().live_allocations == 0,
            "protected host failure releases Lua and wrapper ownership");
  }
}
void independent_contexts() {
  auto domain = bw::memory::Domain::create(std::make_shared<Backing>());
  std::array<std::exception_ptr, 2> failures{};
  std::array<std::jthread, 2> threads;
  for (std::size_t index = 0; index < threads.size(); ++index) {
    threads.at(index) = std::jthread([domain, &failures, index] {
      try {
        bw::script::Context context(domain);
        context.load_source(
            "function run() local t={} for i=1,50 do t[i]=i end value=t[50] "
            "end");
        for (int iteration = 0; iteration < 20; ++iteration) {
          context.start("run");
          require(context.pump().state == bw::script::RunState::complete &&
                      std::get<double>(context.global("value")) == 50,
                  "independent contexts execute with separate allocation "
                  "callbacks");
        }
      } catch (...) {
        failures.at(index) = std::current_exception();
      }
    });
  }
  for (auto& thread : threads) thread.join();
  for (const auto& failure : failures) {
    if (failure) std::rethrow_exception(failure);
  }
  require(domain->snapshot().live_bytes == 0,
          "parallel context teardown returns all charges");
}
}  // namespace
void lua_memory_contracts() {
  coalescing_contract();
  lua_failure_and_wait_contract();
  protected_host_exhaustion();
  independent_contexts();
}
