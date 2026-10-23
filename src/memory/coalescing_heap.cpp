// First-fit physical block implementation; public ownership/failure contract in
// coalescing_heap.hpp. Block extents include headers; every split stays aligned
// and leaves room for header plus payload. Merge updates successor back links.
// Reallocation deliberately allocate-copy-frees, matching recovered Lua calls;
// failure leaves the old block intact. The caller exclusively owns every
// operation, including snapshot and destruction; no internal lock is taken.
// Cross-thread transfer requires external synchronization, not creator
// affinity. No dynamic diagnostic allocations. Design R.1 (#rr-raii), SL.C.1
// (#rclib-jmp), performance EMB.4: bounded backing, no hard-real-time claim.
// Implementation F.6 (#rf-noexcept), MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md): injectable ownership and
// nonthrowing callbacks. Sources: lua-memory-implementation.md; TLSF pinned
// split/coalesce/failure comparison, original first-fit remains authoritative.
// Concurrency revision: design CP.3 (#rconc-data) confines writable state;
// implementation CP.2 (#rconc-races) forbids concurrent access. Both phases
// retrieve MEM.6 (source above): injected Domain backing is separate from this
// context-local algorithm; no shared-heap wrapper is needed by the Lua owner.
#include "bw/memory/coalescing_heap.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <limits>
#include <stdexcept>

namespace bw::memory {
namespace {
constexpr std::size_t kAlignment = 16;
}
struct alignas(kAlignment) CoalescingHeap::Block {
  std::size_t extent;
  std::size_t previous;
  std::size_t next;
  std::size_t requested;
  bool occupied;
};
CoalescingHeap::CoalescingHeap(const std::shared_ptr<Domain>& domain,
                               std::size_t capacity) {
  capacity -= capacity % kAlignment;
  if (!domain || capacity < sizeof(Block) + kAlignment) {
    throw std::invalid_argument("Invalid coalescing heap backing/capacity");
  }
  backing_.reset(domain->allocate_zeroed(capacity, kAlignment));
  storage_ = {static_cast<std::byte*>(backing_.get()), capacity};
  stats_.capacity = capacity;
  // Domain alignment and rounded region bounds provide storage for the header.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  std::construct_at(reinterpret_cast<Block*>(storage_.data()),
                    Block{capacity, capacity, capacity, 0, false});
}
CoalescingHeap::Block& CoalescingHeap::block(
    std::size_t offset) const noexcept {
  if (offset > storage_.size() - sizeof(Block) || offset % kAlignment != 0) {
    std::terminate();
  }
  // Every offset is an aligned header created during initialization/split.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return *reinterpret_cast<Block*>(storage_.subspan(offset).data());
}
void* CoalescingHeap::body(std::size_t offset) const noexcept {
  return storage_.subspan(offset + sizeof(Block)).data();
}
void CoalescingHeap::reject() noexcept {
  if (stats_.rejected_requests != std::numeric_limits<std::size_t>::max()) {
    ++stats_.rejected_requests;
  }
}
void* CoalescingHeap::allocate(std::size_t bytes) noexcept {
  if (bytes == 0 || bytes > storage_.size() - sizeof(Block)) {
    reject();
    return nullptr;
  }
  const auto payload = bytes + ((kAlignment - bytes % kAlignment) % kAlignment);
  if (payload > storage_.size() - sizeof(Block)) {
    reject();
    return nullptr;
  }
  const auto needed = sizeof(Block) + payload;
  for (std::size_t offset = 0; offset < storage_.size();
       offset = block(offset).next) {
    auto& selected = block(offset);
    if (selected.occupied || selected.extent < needed) {
      continue;
    }
    const auto remainder = selected.extent - needed;
    if (remainder >= sizeof(Block) + kAlignment) {
      const auto next = offset + needed;
      // The remainder has checked header/payload space and 16-byte alignment.
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      auto* split = reinterpret_cast<Block*>(storage_.subspan(next).data());
      std::construct_at(split,
                        Block{remainder, offset, selected.next, 0, false});
      if (selected.next < storage_.size()) {
        block(selected.next).previous = next;
      }
      selected.next = next;
      selected.extent = needed;
    }
    selected.occupied = true;
    selected.requested = bytes;
    stats_.live_bytes += bytes;
    stats_.occupied_bytes += selected.extent;
    ++stats_.live_allocations;
    stats_.peak_live_bytes =
        std::max(stats_.peak_live_bytes, stats_.live_bytes);
    stats_.peak_occupied_bytes =
        std::max(stats_.peak_occupied_bytes, stats_.occupied_bytes);
    return body(offset);
  }
  reject();
  return nullptr;
}
std::size_t CoalescingHeap::find(void* pointer) const noexcept {
  for (std::size_t offset = 0; offset < storage_.size();
       offset = block(offset).next) {
    if (body(offset) == pointer && block(offset).occupied) {
      return offset;
    }
  }
  return storage_.size();
}
void CoalescingHeap::merge_next(std::size_t offset) noexcept {
  auto& current = block(offset);
  if (current.next == storage_.size() || block(current.next).occupied) {
    return;
  }
  auto& next = block(current.next);
  current.extent += next.extent;
  current.next = next.next;
  if (current.next < storage_.size()) {
    block(current.next).previous = offset;
  }
  std::destroy_at(&next);
}
bool CoalescingHeap::release(void* pointer) noexcept {
  if (pointer == nullptr) {
    return true;
  }
  auto offset = find(pointer);
  if (offset == storage_.size()) {
    return false;
  }
  auto& current = block(offset);
  stats_.live_bytes -= current.requested;
  stats_.occupied_bytes -= current.extent;
  --stats_.live_allocations;
  current.occupied = false;
  current.requested = 0;
  const auto previous = current.previous;
  if (previous < storage_.size() && !block(previous).occupied) {
    merge_next(previous);
    offset = previous;
  }
  merge_next(offset);
  return true;
}
void* CoalescingHeap::reallocate(void* pointer, std::size_t bytes) noexcept {
  if (bytes == 0) {
    (void)release(pointer);
    return nullptr;
  }
  if (pointer == nullptr) {
    return allocate(bytes);
  }
  const auto offset = find(pointer);
  if (offset == storage_.size()) {
    reject();
    return nullptr;
  }
  const auto copied = std::min(bytes, block(offset).requested);
  auto* replacement = allocate(bytes);
  if (replacement != nullptr) {
    std::memcpy(replacement, pointer, copied);
    (void)release(pointer);
  }
  return replacement;
}
HeapSnapshot CoalescingHeap::snapshot() const {
  auto result = stats_;
  for (std::size_t offset = 0; offset < storage_.size();
       offset = block(offset).next) {
    const auto& current = block(offset);
    if (!current.occupied) {
      const auto available = current.extent - sizeof(Block);
      result.free_bytes += available;
      result.largest_free_bytes =
          std::max(result.largest_free_bytes, available);
    }
  }
  return result;
}
}  // namespace bw::memory
