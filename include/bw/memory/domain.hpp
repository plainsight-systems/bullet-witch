// Owner-aware reclaiming CPU allocation, matching the recovered ordinary heap
// wrapper's zeroing and allocator identity (0x404ab0 / 0x404c40). This is not
// the separate DHeap, frame arena, device allocator or a global new
// replacement. Inputs: shared native provider, explicit limits, byte count and
// power-of-two alignment. Invalid/overflow/exhausted requests return null (try)
// or bad_alloc. Zero-byte requests have distinct releasable storage with zero
// payload charge. Each allocation retains its domain/provider until
// owner-routed release; null release is accepted. Other releases require a live
// pointer from this API, exactly once, after object destruction and all
// consumers have finished. PMR callers must retain a shared domain even while
// their containers are empty; a PMR pointer alone is borrowed. Allocation,
// release and snapshot synchronize; the contained objects do not become
// thread-safe. Pending reservations count against both limits and are exposed
// separately from successful live allocations. Live charges include releases
// until native free returns. Peaks count successful live allocations only.
// Snapshot backing is requested bytes, not usable-size/commitment/residency.
// Algorithm: validate -> reserve under lock -> acquire/initialize outside lock
// -> commit or rollback under lock -> publish. Release frees outside the lock,
// then returns charges. Provider code never runs under the domain mutex.
// Cost: O(bytes) zeroing,
// constant metadata, one native acquire/release; no diagnostic side
// allocations. Domain/provider shared controls are explicit bootstrap
// allocations outside this domain, not recursively charged payload. Design:
// cpp-guidelines R.20 (#rr-owner), E.6 (#re-raii); cpp-performance MEM.10
// (guidelines/memory/MEM.10-debuggable-allocators.md) retains bounded counters,
// with no claim of guard/callstack instrumentation. MEM.1 excludes resets for
// independent lifetimes. Implementation: SL.con.3 (#rsl-bounds) validates size;
// CP.43 (#rconc-time) and CP.22 (#rconc-unknown) keep native calls and filling
// outside bookkeeping locks; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// the native provider.
#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>

#include "bw/memory/cpu_backing.hpp"

namespace bw::memory {
struct Limits {
  std::size_t max_live_bytes = std::numeric_limits<std::size_t>::max();
  std::size_t max_backing_bytes = std::numeric_limits<std::size_t>::max();
};
struct Snapshot {
  std::size_t live_bytes = 0;
  std::size_t backing_bytes = 0;
  std::size_t live_allocations = 0;
  std::size_t peak_live_bytes = 0;
  std::size_t peak_backing_bytes = 0;
  std::size_t rejected_requests = 0;
  std::size_t pending_bytes = 0;
  std::size_t pending_backing_bytes = 0;
  std::size_t pending_allocations = 0;
};
class Domain final : public std::pmr::memory_resource,
                     public std::enable_shared_from_this<Domain> {
  class ConstructionKey {
    friend class Domain;
    ConstructionKey() = default;
  };

 public:
  static std::shared_ptr<Domain> create(std::shared_ptr<CpuBacking> backing,
                                        Limits limits = {});
  Domain(ConstructionKey, std::shared_ptr<CpuBacking> backing, Limits limits);
  [[nodiscard]] void* try_allocate_zeroed(
      std::size_t bytes,
      std::size_t alignment = alignof(std::max_align_t)) noexcept;
  [[nodiscard]] void* allocate_zeroed(
      std::size_t bytes, std::size_t alignment = alignof(std::max_align_t));
  static void release(void* pointer) noexcept;
  [[nodiscard]] Snapshot snapshot() const;

 private:
  void reject() noexcept;
  void* do_allocate(std::size_t bytes, std::size_t alignment) override;
  void do_deallocate(void* pointer, std::size_t bytes,
                     std::size_t alignment) override;
  [[nodiscard]] bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override;
  std::shared_ptr<CpuBacking> backing_;
  Limits limits_;
  mutable std::mutex mutex_;
  Snapshot stats_;
};
}  // namespace bw::memory
