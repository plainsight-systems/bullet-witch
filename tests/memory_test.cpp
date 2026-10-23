// Concurrency refinement: a gated provider observes its calling Domain while
// acquisition/release are paused. Other calls must progress, pending requests
// must consume budget, and failed acquisition must rollback without false
// peaks. Gates use semaphore handoff, not sleep/timing assertions; CTest bounds
// hangs. CP.22 (#rconc-unknown), CP.43 (#rconc-time) and MEM.6
// (guidelines/memory/ MEM.6-inject-allocators-behind-one-interface.md), read
// for both phases, test provider independence and public accounting contracts.
// In-process public memory contracts: zeroing/alignment, bounded failure and
// rollback, original-owner release, retained/empty capabilities, STL/PMR
// propagation and concurrent allocation/free. A bounded fake native provider
// checks exact release tuples; native tests are separately selected on Mac.
// No subprocess/game assets. Design: R.20 (#rr-owner), E.6 (#re-raii) retain
// fixture capabilities; MEM.10
// (guidelines/memory/MEM.10-debuggable-allocators.md) uses bounded bookkeeping.
// Implementation: SL.con.3 (#rsl-bounds) checks extents; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// failure and validates no fallback. Test sizes are contract probes, not
// historical engine budgets or a performance benchmark.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory_resource>
#include <semaphore>
#include <span>
#include <thread>

#include "bw/memory/allocator.hpp"

void device_budget_tests();
#if defined(BW_TEST_MACOS_MEMORY)
void macos_cpu_backing_tests();
void BWMacosTextureAllocationTests();
void media_audio_memory_contracts();
#endif

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
class TestBacking final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    const std::lock_guard lock(mutex_);
    if (fail_.load()) {
      return nullptr;
    }
    for (auto& record : records_) {
      if (record.pointer != nullptr) {
        continue;
      }
      // Fixture native boundary owns raw storage; Domain owns the returned
      // block and will return the identical tuple to deallocate.
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      void* pointer =
          ::operator new(bytes, std::align_val_t(alignment), std::nothrow);
      if (pointer != nullptr) {
        record = {pointer, bytes, alignment};
        ++acquired_;
      }
      return pointer;
    }
    return nullptr;
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    const std::lock_guard lock(mutex_);
    for (auto& record : records_) {
      if (record.pointer != pointer) {
        continue;
      }
      if (record.bytes != bytes || record.alignment != alignment) {
        std::terminate();
      }
      ::operator delete(pointer, std::align_val_t(alignment));
      record = {};
      ++released_;
      return;
    }
    std::terminate();
  }
  void set_failure(bool value) noexcept { fail_ = value; }
  [[nodiscard]] std::size_t acquired() const noexcept {
    return acquired_.load();
  }
  [[nodiscard]] std::size_t released() const noexcept {
    return released_.load();
  }

 private:
  std::atomic<bool> fail_ = false;
  std::atomic<std::size_t> acquired_ = 0;
  std::atomic<std::size_t> released_ = 0;
  struct Record {
    void* pointer = nullptr;
    std::size_t bytes = 0;
    std::size_t alignment = 0;
  };
  std::array<Record, 128> records_{};
  std::mutex mutex_;
};

// One chosen native call pauses; concurrent calls continue to the backing.
// Domain observation happens inside the native callback, proving no domain
// mutex is held there. Observation is read only after entered_ synchronizes.
class GatedBacking final : public bw::memory::CpuBacking {
 public:
  enum class Operation : std::uint8_t { acquire, release };
  explicit GatedBacking(Operation operation, bool fail = false)
      : operation_(operation), fail_(fail) {}
  void observe(std::weak_ptr<bw::memory::Domain> domain) {
    domain_ = std::move(domain);
  }
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    if (operation_ == Operation::acquire && armed_.exchange(false)) {
      pause();
      if (fail_) {
        return nullptr;
      }
    }
    return backing_.allocate(bytes, alignment);
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    if (operation_ == Operation::release && armed_.exchange(false)) {
      pause();
    }
    backing_.deallocate(pointer, bytes, alignment);
  }
  bw::memory::Snapshot wait() {
    entered_.acquire();
    return observed_;
  }
  void resume() { resume_.release(); }

 private:
  void pause() {
    observed_ = domain_.lock()->snapshot();
    entered_.release();
    resume_.acquire();
  }
  Operation operation_;
  bool fail_;
  std::atomic<bool> armed_{true};
  std::weak_ptr<bw::memory::Domain> domain_;
  bw::memory::Snapshot observed_;
  std::binary_semaphore entered_{0}, resume_{0};
  TestBacking backing_;
};

void pending_admission_and_progress() {
  auto backing =
      std::make_shared<GatedBacking>(GatedBacking::Operation::acquire);
  auto domain = bw::memory::Domain::create(backing, {.max_live_bytes = 128});
  backing->observe(domain);
  void* first = nullptr;
  std::jthread worker([&] { first = domain->try_allocate_zeroed(64); });
  const auto observed = backing->wait();
  const auto pending = domain->snapshot();
  void* second = domain->try_allocate_zeroed(64);
  void* excess = domain->try_allocate_zeroed(1);
  const auto overlapped = domain->snapshot();
  backing->resume();
  worker.join();
  const auto committed = domain->snapshot();
  bw::memory::Domain::release(first);
  bw::memory::Domain::release(second);
  bw::memory::Domain::release(excess);
  require(observed.pending_bytes == 64 && observed.live_bytes == 0 &&
              pending.pending_allocations == 1 &&
              pending.pending_backing_bytes > 64 &&
              pending.peak_live_bytes == 0,
          "native acquire and concurrent observer see reservation, not live "
          "storage");
  require(first && second && !excess && overlapped.live_bytes == 64 &&
              overlapped.pending_bytes == 64 &&
              overlapped.peak_live_bytes == 64,
          "another acquire progresses without oversubscribing pending payload");
  require(committed.live_bytes == 128 && committed.pending_allocations == 0 &&
              committed.pending_bytes == 0 &&
              committed.pending_backing_bytes == 0 &&
              domain->snapshot().backing_bytes == 0,
          "successful admission commits once and both owners retire");

  // Set the second domain's backing limit from the public measured request;
  // leave payload headroom to isolate the backing-reservation contract.
  auto bounded_backing =
      std::make_shared<GatedBacking>(GatedBacking::Operation::acquire);
  auto bounded = bw::memory::Domain::create(
      bounded_backing, {128, pending.pending_backing_bytes});
  bounded_backing->observe(bounded);
  void* retained = nullptr;
  std::jthread bounded_worker(
      [&] { retained = bounded->try_allocate_zeroed(64); });
  (void)bounded_backing->wait();
  void* rejected = bounded->try_allocate_zeroed(1);
  bounded_backing->resume();
  bounded_worker.join();
  bw::memory::Domain::release(retained);
  bw::memory::Domain::release(rejected);
  require(retained && !rejected &&
              bounded->snapshot().pending_backing_bytes == 0 &&
              bounded->snapshot().backing_bytes == 0,
          "pending native backing consumes budget independently of payload");
}

void pending_failure_rollback() {
  auto backing =
      std::make_shared<GatedBacking>(GatedBacking::Operation::acquire, true);
  auto domain = bw::memory::Domain::create(backing, {.max_live_bytes = 64});
  backing->observe(domain);
  void* failed = nullptr;
  std::jthread worker([&] { failed = domain->try_allocate_zeroed(48); });
  (void)backing->wait();
  void* survivor = domain->try_allocate_zeroed(16);
  backing->resume();
  worker.join();
  const auto settled = domain->snapshot();
  bw::memory::Domain::release(failed);
  bw::memory::Domain::release(survivor);
  require(
      !failed && survivor && settled.live_bytes == 16 &&
          settled.peak_live_bytes == 16 && settled.live_allocations == 1 &&
          settled.pending_bytes == 0 && settled.pending_backing_bytes == 0 &&
          settled.pending_allocations == 0 && settled.rejected_requests == 1,
      "native failure rolls back only its reservation and leaves successful "
      "owner intact");
  void* retry = domain->try_allocate_zeroed(64);
  bw::memory::Domain::release(retry);
  require(retry && domain->snapshot().backing_bytes == 0,
          "failed acquisition returns budget for a subsequent full request");
}

void release_keeps_budget_until_complete() {
  auto backing =
      std::make_shared<GatedBacking>(GatedBacking::Operation::release);
  auto domain = bw::memory::Domain::create(backing, {.max_live_bytes = 64});
  backing->observe(domain);
  void* pointer = domain->allocate_zeroed(64);
  std::jthread worker([pointer] { bw::memory::Domain::release(pointer); });
  const auto observed = backing->wait();
  const auto releasing = domain->snapshot();
  void* excess = domain->try_allocate_zeroed(1);
  backing->resume();
  worker.join();
  bw::memory::Domain::release(excess);
  const auto released = domain->snapshot();
  require(observed.live_bytes == 64 && releasing.live_bytes == 64 &&
              releasing.live_allocations == 1 && !excess,
          "native free permits observations but retains its admitted budget");
  void* retry = domain->try_allocate_zeroed(64);
  bw::memory::Domain::release(retry);
  require(released.live_bytes == 0 && released.backing_bytes == 0 && retry &&
              domain->snapshot().live_allocations == 0,
          "budget becomes available only after native free completes");
}

void aligned_zeroed_owned_storage() {
  auto backing = std::make_shared<TestBacking>();
  auto domain = bw::memory::Domain::create(backing);
  for (const std::size_t alignment : {1U, 16U, 64U, 4096U}) {
    void* pointer = domain->allocate_zeroed(37, alignment);
    void* aligned = pointer;
    std::size_t extent = 37;
    require(std::align(alignment, 37, aligned, extent) == pointer,
            "requested body alignment");
    const std::span bytes(static_cast<std::byte*>(pointer), 37);
    require(std::ranges::all_of(
                bytes, [](std::byte value) { return value == std::byte{0}; }),
            "ordinary allocation zeroes its body");
    std::ranges::fill(bytes, std::byte{0x7a});
    require(domain->snapshot().live_bytes == 37 &&
                domain->snapshot().backing_bytes > 37,
            "payload and backing accounting are distinct");
    bw::memory::Domain::release(pointer);
  }
  void* first = domain->allocate_zeroed(0);
  void* second = domain->allocate_zeroed(0);
  require(first != second && domain->snapshot().live_bytes == 0 &&
              domain->snapshot().live_allocations == 2,
          "zero-size allocations are independently releasable");
  bw::memory::Domain::release(first);
  bw::memory::Domain::release(second);
  bw::memory::Domain::release(nullptr);
  require(domain->snapshot().backing_bytes == 0 &&
              backing->acquired() == backing->released(),
          "release uses original native tuple and returns charges");
}

void rejection_and_rollback() {
  auto backing = std::make_shared<TestBacking>();
  auto domain = bw::memory::Domain::create(backing, {64, 4096});
  void* retained = domain->allocate_zeroed(48);
  const auto before = domain->snapshot();
  require(domain->try_allocate_zeroed(17) == nullptr,
          "payload budget rejects before acquisition");
  backing->set_failure(true);
  require(domain->try_allocate_zeroed(8) == nullptr,
          "native failure is explicit without fallback");
  backing->set_failure(false);
  require(domain->try_allocate_zeroed(1, 3) == nullptr &&
              domain->try_allocate_zeroed(1, 0) == nullptr &&
              domain->try_allocate_zeroed(
                  std::numeric_limits<std::size_t>::max()) == nullptr,
          "invalid alignment and overflowing extent rejected");
  const auto after = domain->snapshot();
  require(after.live_bytes == before.live_bytes &&
              after.backing_bytes == before.backing_bytes &&
              after.live_allocations == before.live_allocations &&
              after.peak_backing_bytes == before.peak_backing_bytes &&
              after.rejected_requests > before.rejected_requests,
          "failure rolls back charges and does not inflate successful peak");
  auto too_small = bw::memory::Domain::create(backing, {64, 1});
  require(too_small->try_allocate_zeroed(1) == nullptr,
          "native backing budget includes header and alignment");
  bool threw = false;
  try {
    (void)domain->allocate_zeroed(65);
  } catch (const std::bad_alloc&) {
    threw = true;
  }
  require(threw, "throwing wrapper reports allocation failure");
  bw::memory::Domain::release(retained);
  void* retry = domain->allocate_zeroed(64);
  bw::memory::Domain::release(retry);
  require(domain->snapshot().live_allocations == 0,
          "budget becomes available after release");
}

void retained_capabilities_and_containers() {
  auto backing = std::make_shared<TestBacking>();
  auto domain = bw::memory::Domain::create(backing);
  std::weak_ptr<bw::memory::Domain> weak = domain;
  void* pointer = domain->allocate_zeroed(19);
  domain.reset();
  require(!weak.expired(),
          "allocation retains domain after external owner exits");
  bw::memory::Domain::release(pointer);
  require(weak.expired(), "last allocation releases domain capability");
  {
    auto first = bw::memory::Domain::create(backing);
    auto second = bw::memory::Domain::create(backing);
    weak = first;
    bw::memory::Vector<int> empty{bw::memory::Allocator<int>(first)};
    first.reset();
    require(!weak.expired(), "empty owning container retains capability");
    empty.assign({1, 2, 3});
    bw::memory::Vector<int> copied = empty;
    require(copied.get_allocator() == empty.get_allocator(),
            "copy construction preserves domain");
    bw::memory::Vector<int> other{bw::memory::Allocator<int>(second)};
    other.assign({9, 8});
    other = empty;
    require(other.get_allocator() == empty.get_allocator() &&
                second->snapshot().live_bytes == 0,
            "copy assignment changes allocator and frees previous owner");
    auto moved = std::move(copied);
    require(moved == empty, "move preserves owned contents");
    std::pmr::vector<int> pmr(second.get());
    pmr.assign({4, 5, 6});
    require(second->snapshot().live_bytes != 0, "PMR routes through domain");
  }
  require(weak.expired(),
          "last container frees retained empty-owner capability");
  std::weak_ptr<int> weak_value;
  {
    auto shared_domain = bw::memory::Domain::create(backing);
    weak = shared_domain;
    auto value =
        std::allocate_shared<int>(bw::memory::Allocator<int>(shared_domain), 7);
    weak_value = value;
    shared_domain.reset();
    value.reset();
    require(!weak.expired(), "weak shared control retains domain storage");
  }
  weak_value.reset();
  require(weak.expired(),
          "final weak control releases domain and native backing");
  bw::memory::Vector<int> legacy{1, 2};
  require(!legacy.get_allocator().domain(),
          "legacy parser allocation remains explicit");
  bool rejected_null = false;
  try {
    bw::memory::Allocator<int> invalid{std::shared_ptr<bw::memory::Domain>{}};
  } catch (const std::invalid_argument&) {
    rejected_null = true;
  }
  require(rejected_null && backing->acquired() == backing->released(),
          "null explicit domain rejected and owned storage reclaimed");
}

void cross_thread_ownership() {
  auto backing = std::make_shared<TestBacking>();
  auto domain = bw::memory::Domain::create(backing);
  std::array<void*, 4> transferred{};
  std::array<std::jthread, 4> workers;
  for (std::size_t index = 0; index < workers.size(); ++index) {
    workers.at(index) = std::jthread([domain, &transferred, index] {
      for (std::size_t iteration = 0; iteration < 64; ++iteration) {
        void* block = domain->allocate_zeroed(31, 64);
        bw::memory::Domain::release(block);
      }
      transferred.at(index) = domain->allocate_zeroed(index + 1);
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
  require(domain->snapshot().live_allocations == transferred.size(),
          "concurrent allocations retain independent live charges");
  for (auto* pointer : transferred) {
    bw::memory::Domain::release(pointer);
  }
  require(domain->snapshot().live_allocations == 0 &&
              domain->snapshot().backing_bytes == 0 &&
              backing->acquired() == backing->released(),
          "cross-thread releases reclaim through original owner");
}
}  // namespace

int main() {
  try {
    aligned_zeroed_owned_storage();
    rejection_and_rollback();
    retained_capabilities_and_containers();
    cross_thread_ownership();
    pending_admission_and_progress();
    pending_failure_rollback();
    release_keeps_budget_until_complete();
    device_budget_tests();
#if defined(BW_TEST_MACOS_MEMORY)
    macos_cpu_backing_tests();
    BWMacosTextureAllocationTests();
    media_audio_memory_contracts();
#endif
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
