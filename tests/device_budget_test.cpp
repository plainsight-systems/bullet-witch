// In-process device accounting contracts, extending the existing memory test
// executable. No GPU/device claims: fake backing owns CPU metadata only. Verify
// pending/live distinction, transactional reconciliation, overflow, move and
// retained lifetime. E.6 (#re-raii), SL.con.3 (#rsl-bounds), GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) applied
// from both MCP phases; sizes are explicit test limits, not historical budgets.
#include "bw/memory/device_budget.hpp"

#include <limits>
#include <memory_resource>
#include <stdexcept>
#include <utility>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
class CpuTestBacking final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
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
};
}  // namespace
void device_budget_tests() {
  using bw::memory::DeviceBudget;
  auto domain = bw::memory::Domain::create(std::make_shared<CpuTestBacking>());
  auto budget = DeviceBudget::create(
      domain, {.max_logical_bytes = 100, .max_native_bytes = 200});
  {
    auto charge = budget->reserve(40, 60);
    auto stats = budget->snapshot();
    require(stats.pending_logical_bytes == 40 &&
                stats.pending_native_bytes == 60 && stats.live_resources == 0,
            "Pending estimates are separate from live device storage");
    charge.commit(80);
    stats = budget->snapshot();
    require(stats.pending_resources == 0 && stats.live_resources == 1 &&
                stats.live_logical_bytes == 40 && stats.live_native_bytes == 80,
            "Reported native size replaces estimate at commit");
    auto pending = budget->reserve(30, 100);
    bool failed = false;
    try {
      pending.commit(121);
    } catch (const std::bad_alloc&) {
      failed = true;
    }
    stats = budget->snapshot();
    require(failed && stats.pending_native_bytes == 100 &&
                stats.live_native_bytes == 80,
            "Rejected reconciliation preserves reservation for rollback");
    pending.reset();
    auto moved = std::move(charge);
    require(moved && budget->snapshot().live_resources == 1,
            "Moving charge transfers exactly one ownership claim");
    auto replacement = budget->reserve(10, 20);
    replacement.commit(10);
    moved = std::move(replacement);
    require(budget->snapshot().live_resources == 1 &&
                budget->snapshot().live_native_bytes == 10,
            "Move assignment releases previous charge before adoption");
    failed = false;
    try {
      moved.commit(10);
    } catch (const std::logic_error&) {
      failed = true;
    }
    require(failed, "Committed charge cannot be published twice");
  }
  auto stats = budget->snapshot();
  require(stats.pending_resources == 0 && stats.live_resources == 0 &&
              stats.pending_native_bytes == 0 && stats.live_native_bytes == 0,
          "All pending and committed charges return on teardown");
  bool failed = false;
  try {
    (void)budget->reserve(101, 0);
  } catch (const std::bad_alloc&) {
    failed = true;
  }
  require(failed && budget->snapshot().pending_resources == 0,
          "Logical admission failure is atomic");
  auto uncapped = DeviceBudget::create(domain);
  auto huge = uncapped->reserve(std::numeric_limits<std::size_t>::max(), 0);
  failed = false;
  try {
    (void)uncapped->reserve(1, 0);
  } catch (const std::bad_alloc&) {
    failed = true;
  }
  require(failed, "Logical admission rejects arithmetic overflow");
  huge.reset();
  auto retained = uncapped->reserve(1, 2);
  retained.commit(3);
  std::weak_ptr<DeviceBudget> observed = uncapped;
  uncapped.reset();
  require(!observed.expired(), "Charge retains its accounting owner");
  retained.reset();
  require(observed.expired(), "Final charge releases accounting owner");
  observed.reset();
  budget.reset();
  require(domain->snapshot().live_bytes == 0,
          "Budget CPU metadata has matching domain release");
}
