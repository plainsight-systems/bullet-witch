#include "crash_allocator.hpp"

#include "allocator_checks.hpp"
#include "binned/binned_allocator.hpp"
#include "bw/memory/alignment.hpp"
#include "bw/platform/memory/allocator_platform.hpp"
#include "bw/platform/memory/memory_failure.hpp"
#include <algorithm>
#include <cstring>
#include <memory>

// Core R.1/R.15: crash reserves and activation lock deliberately live until
// process termination; storage is never freed through the wrong allocator.
// Performance MEM.2/MEM.5: source pool classes and promotion are retained, not
// replaced by a generic free-list or power-of-two classification.
// Unreal reference: f45729c0b6f097038e72b52d366a62baeca3275b.
// Method references below are Private/GenericPlatform/GenericPlatformMallocCrash.cpp
// unless an explicit public header is named.
namespace bw::memory {
namespace {
constexpr std::uint32_t large_capacity = 2 * 1024 * 1024;
constexpr std::uint32_t required_alignment = 16;
struct PoolDescription {
  std::uint32_t bytes;
  std::uint32_t count;
};
// AllPoolDesc:74-90: Mac takes multiplier one; these are reference configuration
// values, not workload-derived project sizes.
constexpr std::array<PoolDescription, 14> descriptions{{
    {64, 224},
    {96, 144},
    {128, 80},
    {192, 560},
    {256, 384},
    {384, 208},
    {512, 48},
    {768, 32},
    {1024, 32},
    {2048, 32},
    {4096, 32},
    {8192, 32},
    {16384, 16},
    {32768, 16},
}};
// IsActive:13-14,323-326: independent state permits observation without lazy
// singleton construction. Atomic adaptation permits concurrent observation.
std::atomic<bool> active{false};

} // namespace

// FMallocCrashPool constructor:120-144: carve two fixed regions and construct
// records containing each slot address; the view never owns separate storage.
void CrashAllocator::Pool::initialize(std::uint32_t bytes, std::uint32_t count,
                                      CrashAllocator& owner) {
  slot_size = bytes;
  const auto records_bytes = count * static_cast<std::uint32_t>(sizeof(Slot));
  auto* records = reinterpret_cast<Slot*>(owner.take_bookkeeping_storage(records_bytes));
  slots = {records, count};
  base = owner.take_small_storage(bytes * count);
  for (std::uint32_t index = 0; index < count; ++index) {
    std::construct_at(&slots[index], Slot{0, base + static_cast<std::size_t>(index) * bytes});
  }
  allocated_bytes = bytes * count + records_bytes;
}

// AllocateFromPool:146-184: first zero-size record wins. Source usage counters
// and fill apply to requested bytes, not the whole size class.
void* CrashAllocator::Pool::allocate(std::uint32_t bytes) {
  debug_verify();
  for (std::size_t index = 0; index < slots.size(); ++index) {
    auto& slot = slots[index];
    if (slot.bytes == 0) {
      maximum_index = std::max(maximum_index, static_cast<std::uint32_t>(index));
      ++used;
      ++total_used;
      maximum_used = std::max(maximum_used, used);
      slot.bytes = bytes;
      std::memset(slot.pointer, 0xfe, bytes);
      debug_verify();
      return slot.pointer;
    }
  }
  // Source diagnoses/exits on inconsistent pool exhaustion; use the accepted
  // headless critical route, never fallback to an ordinary heap.
  BW_ALLOCATOR_REQUIRE(false && "crash small pool exhausted");
}

// TryFreeFromPool:187-211: locate slot by its offset, wipe the recorded request,
// then clear occupancy. This does not destroy a caller's C++ object.
void CrashAllocator::Pool::release(void* pointer) {
  const auto offset =
      reinterpret_cast<std::uintptr_t>(pointer) - reinterpret_cast<std::uintptr_t>(base);
  const auto index = offset / slot_size;
  BW_ALLOCATOR_REQUIRE(index < slots.size());
  auto& slot = slots[index];
  std::memset(slot.pointer, 0xcd, static_cast<std::size_t>(slot.bytes));
  slot.bytes = 0;
  --used;
  debug_verify();
}

// FMallocCrashPool::GetAllocationSize:213-222: return requested bytes from the
// matching record. Valid slot addresses are the allocator caller's contract.
std::uint64_t CrashAllocator::Pool::allocation_size(void* pointer) const {
  const auto offset =
      reinterpret_cast<std::uintptr_t>(pointer) - reinterpret_cast<std::uintptr_t>(base);
  const auto index = offset / slot_size;
  BW_ALLOCATOR_REQUIRE(index < slots.size());
  return slots[index].bytes;
}

// DebugVerify:236-246: debug-only small-request metadata upper-bound check.
void CrashAllocator::Pool::debug_verify() const {
#ifndef NDEBUG
  for (const auto& slot : slots) {
    BW_ALLOCATOR_CHECK(slot.bytes <= 32768);
  }
#endif
}

// Constructor:255-293: reserve all three OS regions before crash activation,
// verify successful 16-byte alignment, then initialize each small pool.
CrashAllocator::CrashAllocator(BinnedAllocator* previous)
    : small_bytes_(small_storage_size()), bookkeeping_bytes_(bookkeeping_storage_size()),
      previous_(previous) {
  static_assert(sizeof(Slot) == 16);
  large_ = static_cast<std::byte*>(
      platform::allocate_crash_pool_from_os(align_up(large_capacity, safe_page_size())));
  small_ = static_cast<std::byte*>(platform::allocate_crash_pool_from_os(small_bytes_));
  bookkeeping_ = static_cast<std::byte*>(platform::allocate_crash_pool_from_os(bookkeeping_bytes_));
  BW_ALLOCATOR_REQUIRE(large_ != nullptr && small_ != nullptr && bookkeeping_ != nullptr);
  BW_ALLOCATOR_REQUIRE(reinterpret_cast<std::uintptr_t>(large_) % required_alignment == 0 &&
                       reinterpret_cast<std::uintptr_t>(small_) % required_alignment == 0 &&
                       reinterpret_cast<std::uintptr_t>(bookkeeping_) % required_alignment == 0);
  initialize_pools();
}

// Destructor:296-303: pool descriptors are destroyed in reverse order. Their
// fixed member array does this automatically; source does not release reserves.
CrashAllocator::~CrashAllocator() = default;

// Get:305-309: first call constructs the singleton with the previous allocator.
// Later calls neither reset pools nor overwrite that original allocator pointer.
CrashAllocator& CrashAllocator::get(BinnedAllocator* previous) {
  static CrashAllocator instance(previous);
  return instance;
}

// SetAsGMalloc:311-320: retain activation ownership permanently, atomically swap
// the facade, then record crashed thread/active. No fixed-GMalloc build selected.
void CrashAllocator::activate() {
  activation_mutex_.lock();
  replace_general_allocator_for_crash(*this);
  crashed_thread_.store(platform::current_allocator_thread_id(), std::memory_order_relaxed);
  active.store(true, std::memory_order_release);
}

// IsActive:323-326: observation does not force allocator bootstrap.
bool CrashAllocator::is_active() { return active.load(std::memory_order_acquire); }

// Malloc:328-371: source 32-bit request, alignment limit and crashed-thread gate;
// use first fitting available pool, then the reserved large bump area.
void* CrashAllocator::allocate(std::size_t bytes, std::uint32_t alignment) {
  const auto requested = static_cast<std::uint32_t>(bytes);
  BW_ALLOCATOR_REQUIRE(alignment <= required_alignment);
  check_allocation_thread();
  if (auto* pool = choose_pool(requested)) {
    return pool->allocate(requested);
  }
  // Malloc:347-360: 16-byte cursor alignment and a size/pointer prefix. Source
  // does not fill large allocations or reclaim them when free is called.
  const auto with_header = requested + static_cast<std::uint32_t>(sizeof(Slot));
  large_offset_ = align_up(large_offset_, required_alignment);
  BW_ALLOCATOR_REQUIRE(large_offset_ + with_header <= large_capacity);
  auto* record_address = large_ + large_offset_;
  large_offset_ += with_header;
  auto* record = std::construct_at(reinterpret_cast<Slot*>(record_address),
                                   Slot{requested, record_address + sizeof(Slot)});
  return record->pointer;
}

// Realloc:374-432: always allocate/copy/free for nonzero existing data. Previous
// Binned metadata is the selected supported source family; unknown heaps are not
// accepted. The source ignores the requested alignment here and uses 16.
void* CrashAllocator::reallocate(void* pointer, std::size_t bytes, std::uint32_t) {
  check_allocation_thread();
  if (pointer == nullptr) {
    return allocate(bytes, required_alignment);
  }
  if (bytes == 0) {
    release(pointer);
    return nullptr;
  }
  std::size_t old_bytes = 0;
  // Realloc:382-408: source tests owned ranges before consulting known previous
  // allocator metadata. The type boundary substitutes its descriptive-name list.
  if (!in_small_pool(pointer) && !in_large_pool(pointer)) {
    BW_ALLOCATOR_REQUIRE(previous_ != nullptr);
    BW_ALLOCATOR_REQUIRE(previous_->allocation_size(pointer, old_bytes) && old_bytes != 0);
  } else {
    old_bytes = static_cast<std::size_t>(recorded_size(pointer));
  }
  // Realloc:411-419: copy bounded payload; release intentionally leaves old-heap
  // allocations untouched once the crash allocator has taken over.
  void* result = allocate(bytes, required_alignment);
  std::memcpy(result, pointer, std::min(bytes, old_bytes));
  release(pointer);
  return result;
}

// Free:435-460 plus CheckThreadForFree:505-525: default-mode non-owner frees
// return immediately. Owner frees recycle small slots; large/previous storage
// stays reserved exactly as in the source crash path.
void CrashAllocator::release(void* pointer) {
  if (is_owner_thread() && in_small_pool(pointer)) {
    find_pool(pointer).release(pointer);
  }
}

// GetAllocationSize:540-543: return true even for unknown/null pointers; their
// source recorded-size helper supplies zero rather than querying the old heap.
bool CrashAllocator::allocation_size(void* pointer, std::size_t& bytes) {
  bytes = static_cast<std::size_t>(recorded_size(pointer));
  return true;
}

// Public/GenericPlatformMallocCrash.h:103-106: only the crashed thread mutates
// allocation state; the source therefore advertises internal thread safety.
bool CrashAllocator::internally_thread_safe() const { return true; }

// CheckThreadForAllocation:483-503: in the selected default mode other threads
// suspend forever; no retry, fallback allocation or alternate owner is allowed.
void CrashAllocator::check_allocation_thread() const {
  if (!is_owner_thread()) {
    platform::wait_after_memory_failure();
  }
}

// Shared extraction of the identical thread comparison in source lines486/508.
bool CrashAllocator::is_owner_thread() const {
  return crashed_thread_.load(std::memory_order_relaxed) == platform::current_allocator_thread_id();
}

// IsPtrInSmallPool:534-538: unsigned address ordering expresses native address
// membership without relational comparisons between unrelated C++ objects.
bool CrashAllocator::in_small_pool(void* pointer) const {
  const auto address = reinterpret_cast<std::uintptr_t>(pointer);
  const auto start = reinterpret_cast<std::uintptr_t>(small_);
  return address >= start && address - start < small_bytes_;
}

// IsPtrInLargePool:528-532: range includes the entire source 2MiB reserve.
bool CrashAllocator::in_large_pool(void* pointer) const {
  const auto address = reinterpret_cast<std::uintptr_t>(pointer);
  const auto start = reinterpret_cast<std::uintptr_t>(large_);
  return address >= start && address - start < large_capacity;
}

// ChoosePoolForSize:593-613: exhausted pools are skipped, including a fitting
// class; this explicitly promotes into larger classes before the large reserve.
CrashAllocator::Pool* CrashAllocator::choose_pool(std::uint32_t bytes) {
  for (auto& pool : pools_) {
    if (pool.used < pool.slots.size() && bytes <= pool.slot_size) {
      return &pool;
    }
  }
  return nullptr;
}

// FindPoolForAlloc:615-620: upper-bound binary search on sorted base addresses,
// then take the preceding pool. std::upper_bound substitutes the same algorithm
// for Unreal Algo::UpperBoundBy; no allocation or linear scan is introduced.
CrashAllocator::Pool& CrashAllocator::find_pool(void* pointer) {
  const auto address = reinterpret_cast<std::uintptr_t>(pointer);
  const auto beyond = std::upper_bound(pools_.begin(), pools_.end(), address,
                                       [](std::uintptr_t value, const Pool& pool) {
                                         return value < reinterpret_cast<std::uintptr_t>(pool.base);
                                       });
  return *(beyond - 1);
}

// AllocateFromSmallPool:622-633: carve constructor storage monotonically.
std::byte* CrashAllocator::take_small_storage(std::uint32_t bytes) {
  if (small_offset_ + bytes <= small_bytes_) {
    auto* result = small_ + small_offset_;
    small_offset_ += bytes;
    return result;
  }
  BW_ALLOCATOR_CHECK(false);
  return nullptr;
}

// AllocateFromBookkeeping:635-646: independent bump range for slot records.
std::byte* CrashAllocator::take_bookkeeping_storage(std::uint32_t bytes) {
  if (bookkeeping_offset_ + bytes <= bookkeeping_bytes_) {
    auto* result = bookkeeping_ + bookkeeping_offset_;
    bookkeeping_offset_ += bytes;
    return result;
  }
  BW_ALLOCATOR_CHECK(false);
  return nullptr;
}

// GetAllocationSize:648-662: small requests use the slot record; large requests
// use the immediately preceding prefix. Everything else has no recorded size.
std::uint64_t CrashAllocator::recorded_size(void* pointer) {
  if (in_small_pool(pointer)) {
    return find_pool(pointer).allocation_size(pointer);
  }
  if (in_large_pool(pointer)) {
    const auto* record =
        reinterpret_cast<const Slot*>(static_cast<std::byte*>(pointer) - sizeof(Slot));
    return record->bytes;
  }
  return 0;
}

// SafePageSize:664-671: native page size, or source 64KiB fallback before native
// constants are initialized. No game-asset allocation is involved.
std::uint32_t CrashAllocator::safe_page_size() {
  const auto page = platform::allocator_constants().native_page_size;
  return page == 0 ? 65536 : page;
}

// CalculateSmallPoolTotalSize:546-562: sum exact descriptor products and round
// once to native pages. Singleton construction needs no separate mutable cache.
std::uint32_t CrashAllocator::small_storage_size() {
  std::uint32_t bytes = 0;
  for (const auto& description : descriptions) {
    BW_ALLOCATOR_CHECK(description.count % 16 == 0 && description.bytes % 16 == 0);
    bytes += description.bytes * description.count;
  }
  return align_up(bytes, safe_page_size());
}

// CalculateBookkeepingPoolTotalSize:565-581: one size/pointer record per slot,
// rounded to page size independently of the small allocation region.
std::uint32_t CrashAllocator::bookkeeping_storage_size() {
  std::uint32_t bytes = 0;
  for (const auto& description : descriptions) {
    BW_ALLOCATOR_CHECK(description.count % 16 == 0 && description.bytes % 16 == 0);
    bytes += description.count * static_cast<std::uint32_t>(sizeof(Slot));
  }
  return align_up(bytes, safe_page_size());
}

// InitializeSmallPools:583-591: source table order also establishes ordered bases
// used by the pointer upper-bound search. No per-pool heap allocation occurs.
void CrashAllocator::initialize_pools() {
  for (std::size_t index = 0; index < pools_.size(); ++index) {
    pools_[index].initialize(descriptions[index].bytes, descriptions[index].count, *this);
  }
  BW_ALLOCATOR_CHECK(small_offset_ <= small_bytes_);
}

// Public/HAL/MemoryBase.h:145-148, FMalloc::QuantizeSize inherited by crash heap.
std::size_t CrashAllocator::quantize(std::size_t bytes, std::uint32_t) const { return bytes; }
// MemoryBase.h:166-168, FMalloc::Trim: crash heap has no reclaimable thread caches.
void CrashAllocator::trim(bool) {}
// MemoryBase.h:173-175, FMalloc::SetupTLSCachesOnCurrentThread inherited unchanged.
void CrashAllocator::setup_current_thread_cache() {}
// MemoryBase.h:180-182, FMalloc::MarkTLSCachesAsUsedOnCurrentThread inherited unchanged.
void CrashAllocator::mark_current_thread_cache_used() {}
// MemoryBase.h:187-189, FMalloc::MarkTLSCachesAsUnusedOnCurrentThread inherited unchanged.
void CrashAllocator::mark_current_thread_cache_unused() {}
// MemoryBase.h:194-196, FMalloc::ClearAndDisableTLSCachesOnCurrentThread inherited unchanged.
void CrashAllocator::clear_current_thread_cache() {}
} // namespace bw::memory
