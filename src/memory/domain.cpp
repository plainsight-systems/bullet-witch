// Ordinary owner-aware allocation implementation; contract in domain.hpp.
// Recovered 0x404ab0 zeroing and 0x404c40 original-owner release are preserved;
// native pointer width and checked extent arithmetic replace the x86 prefix
// ABI. Validate/round prefix, reserve budget, acquire backing, install shared
// owner, zero payload; release retains owner locally before destroying
// header/backing. Pending reservations count against limits while native calls
// and zeroing run outside the mutex. Commit moves pending to live; failure
// rolls back without inflating successful peaks. Free keeps its live charge
// until the native release finishes. Snapshots remain available throughout.
// CP.22 (#rconc-unknown) and CP.43 (#rconc-time), retrieved in both concurrency
// phases, separate virtual provider calls from short accounting transactions.
// Constant metadata, O(payload) filling, no
// diagnostic allocation. Design: R.20 (#rr-owner), E.6 (#re-raii) retain the
// original domain; MEM.10 (guidelines/memory/MEM.10-debuggable-allocators.md)
// provides bounded counters without claiming guard/callstack coverage.
// Implementation queries: SL.con.3
// (#rsl-bounds) validates addition/rounding; CP.43 (#rconc-time) bounds lock
// scope; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) selects
// an injected native provider. See memory-bullet-witch-evidence.md and
// memory-unreal-reference.md for pinned ordinary allocation/reference
// contracts.
#include "bw/memory/domain.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>

namespace bw::memory {
namespace {
struct Header {
  std::shared_ptr<Domain> owner;
  void* raw;
  std::size_t backing_bytes;
  std::size_t alignment;
  std::size_t payload_bytes;
};
void increment_saturated(std::size_t& value) noexcept {
  if (value != std::numeric_limits<std::size_t>::max()) {
    ++value;
  }
}
}  // namespace

std::shared_ptr<Domain> Domain::create(std::shared_ptr<CpuBacking> backing,
                                       Limits limits) {
  if (!backing) {
    throw std::invalid_argument("Null CPU backing provider");
  }
  return std::make_shared<Domain>(ConstructionKey{}, std::move(backing),
                                  limits);
}
Domain::Domain(ConstructionKey, std::shared_ptr<CpuBacking> backing,
               Limits limits)
    : backing_(std::move(backing)), limits_(limits) {}

void Domain::reject() noexcept {
  const std::lock_guard lock(mutex_);
  increment_saturated(stats_.rejected_requests);
}

void* Domain::try_allocate_zeroed(std::size_t bytes,
                                  std::size_t alignment) noexcept {
  auto owner = weak_from_this().lock();
  constexpr auto maximum = std::numeric_limits<std::size_t>::max();
  if (!owner || !std::has_single_bit(alignment)) {
    reject();
    return nullptr;
  }
  alignment = std::max(alignment, alignof(std::max_align_t));
  if (alignment - 1 > maximum - sizeof(Header)) {
    reject();
    return nullptr;
  }
  const auto prefix = (sizeof(Header) + alignment - 1) & ~(alignment - 1);
  const auto body = std::max(bytes, std::size_t{1});
  if (body > maximum - prefix) {
    reject();
    return nullptr;
  }
  const auto total = prefix + body;
  {
    const std::lock_guard lock(mutex_);
    if (bytes >
            limits_.max_live_bytes - stats_.live_bytes - stats_.pending_bytes ||
        total > limits_.max_backing_bytes - stats_.backing_bytes -
                    stats_.pending_backing_bytes ||
        stats_.live_allocations == maximum - stats_.pending_allocations) {
      increment_saturated(stats_.rejected_requests);
      return nullptr;
    }
    stats_.pending_bytes += bytes;
    stats_.pending_backing_bytes += total;
    ++stats_.pending_allocations;
  }
  void* raw = backing_->allocate(total, alignment);
  if (raw == nullptr) {
    const std::lock_guard lock(mutex_);
    stats_.pending_bytes -= bytes;
    stats_.pending_backing_bytes -= total;
    --stats_.pending_allocations;
    increment_saturated(stats_.rejected_requests);
    return nullptr;
  }
  const std::span storage(static_cast<std::byte*>(raw), total);
  // Prefix rounding leaves an aligned, bounded region for this header object.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  auto* metadata = reinterpret_cast<Header*>(
      storage.subspan(prefix - sizeof(Header)).data());
  std::construct_at(metadata,
                    Header{std::move(owner), raw, total, alignment, bytes});
  void* result = storage.subspan(prefix).data();
  std::memset(result, 0, body);
  {
    const std::lock_guard lock(mutex_);
    stats_.pending_bytes -= bytes;
    stats_.pending_backing_bytes -= total;
    --stats_.pending_allocations;
    stats_.live_bytes += bytes;
    stats_.backing_bytes += total;
    ++stats_.live_allocations;
    stats_.peak_live_bytes =
        std::max(stats_.peak_live_bytes, stats_.live_bytes);
    stats_.peak_backing_bytes =
        std::max(stats_.peak_backing_bytes, stats_.backing_bytes);
  }
  return result;
}

void* Domain::allocate_zeroed(std::size_t bytes, std::size_t alignment) {
  auto* result = try_allocate_zeroed(bytes, alignment);
  if (result == nullptr) {
    throw std::bad_alloc();
  }
  return result;
}

void Domain::release(void* pointer) noexcept {
  if (pointer == nullptr) {
    return;
  }
  // The API's live-pointer precondition guarantees this immediately preceding
  // header; no arbitrary-pointer or double-free validation is claimed.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  auto* metadata = static_cast<Header*>(pointer) - 1;
  auto owner = std::move(metadata->owner);
  void* raw = metadata->raw;
  const auto total = metadata->backing_bytes;
  const auto alignment = metadata->alignment;
  const auto payload = metadata->payload_bytes;
  std::destroy_at(metadata);
  owner->backing_->deallocate(raw, total, alignment);
  const std::lock_guard lock(owner->mutex_);
  owner->stats_.live_bytes -= payload;
  owner->stats_.backing_bytes -= total;
  --owner->stats_.live_allocations;
}

Snapshot Domain::snapshot() const {
  const std::lock_guard lock(mutex_);
  return stats_;
}
void* Domain::do_allocate(std::size_t bytes, std::size_t alignment) {
  return allocate_zeroed(bytes, alignment);
}
void Domain::do_deallocate(void* pointer, std::size_t, std::size_t) {
  release(pointer);
}
bool Domain::do_is_equal(
    const std::pmr::memory_resource& other) const noexcept {
  return this == &other;
}
}  // namespace bw::memory
