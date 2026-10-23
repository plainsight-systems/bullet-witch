// Bounded reclaiming storage for the recovered Lua context (0x564b90/0x564b10).
// Owns one Domain-backed region; every request is aligned to 16 bytes. The heap
// must outlive all returned raw storage; clients destroy objects before
// release. No reset, growth, relocation or fallback. Size includes native block
// metadata. Allocation/reallocation return null on exhaustion/overflow; failed
// replacement leaves old bytes allocated. Zero reallocation frees. Release
// accepts null; unmatched pointers return false. Stale pointers after reuse
// remain invalid. Single exclusive owner: no concurrent calls, including
// snapshot and destruction. There is no internal synchronization or creator-
// thread affinity; sequential ownership transfer requires external
// synchronization. Destruction requires all users quiesced and releases
// backing. Separate heaps may run concurrently against the shared Domain.
// Clients own object lifetimes. Algorithm: first-fit scan -> trailing split ->
// occupy; free merges adjacent free blocks; realloc allocates, copies
// min(old,new), then frees old. Snapshot scans free ranges. O(blocks + copied
// bytes), fixed in-region metadata, no per-operation external allocation.
// Capacity rounds DOWN to 16-byte units. Design: R.1 (#rr-raii) owns backing,
// SL.C.1
// (#rclib-jmp) keeps allocator outside Lua's longjmp frames; EMB.4
// (guidelines/embedded/EMB.4-use-a-deterministic-allocator-when-allocation-needed.md)
// motivates bounded failure, not replacement of recovered first fit with TLSF.
// Implementation: F.6 (#rf-noexcept) keeps callbacks nonthrowing; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// Domain. See research/engine/subsystems/lua-memory-implementation.md for
// source pins.
// Concurrency revision: design CP.3 (#rconc-data) confines mutable heap state;
// implementation CP.2 (#rconc-races) requires externally ordered access, even
// for snapshots. Both phases retrieve MEM.6 (source above): Domain remains the
// injected backing boundary; it does not make this context-local heap shared.
#pragma once

#include <memory>
#include <span>

#include "bw/memory/domain.hpp"

namespace bw::memory {
struct HeapSnapshot {
  std::size_t capacity = 0;
  std::size_t live_bytes = 0;
  std::size_t peak_live_bytes = 0;
  std::size_t occupied_bytes = 0;
  std::size_t peak_occupied_bytes = 0;
  std::size_t live_allocations = 0;
  std::size_t free_bytes = 0;
  std::size_t largest_free_bytes = 0;
  std::size_t rejected_requests = 0;
};
class CoalescingHeap final {
 public:
  CoalescingHeap(const std::shared_ptr<Domain>& domain, std::size_t capacity);
  ~CoalescingHeap() = default;
  CoalescingHeap(const CoalescingHeap&) = delete;
  CoalescingHeap& operator=(const CoalescingHeap&) = delete;
  CoalescingHeap(CoalescingHeap&&) = delete;
  CoalescingHeap& operator=(CoalescingHeap&&) = delete;
  [[nodiscard]] void* allocate(std::size_t bytes) noexcept;
  [[nodiscard]] void* reallocate(void* pointer, std::size_t bytes) noexcept;
  bool release(void* pointer) noexcept;
  [[nodiscard]] HeapSnapshot snapshot() const;

 private:
  struct Block;
  struct ReleaseBacking {
    void operator()(void* pointer) const noexcept { Domain::release(pointer); }
  };
  Block& block(std::size_t offset) const noexcept;
  void* body(std::size_t offset) const noexcept;
  std::size_t find(void* pointer) const noexcept;
  void merge_next(std::size_t offset) noexcept;
  void reject() noexcept;
  std::unique_ptr<void, ReleaseBacking> backing_;
  std::span<std::byte> storage_;
  HeapSnapshot stats_;
};
}  // namespace bw::memory
