#include "binned/binned_allocator.hpp"
#include "allocator_checks.hpp"
#include "bw/tasks/named_thread_dispatch.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>

// Core CP.2/C.32/R.15: locked metadata owns process-lifetime VM; client slots
// have explicit release. Performance MEM.2/MEM.5: intrusive class pools and
// direct large VM. References: Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// Private/HAL/MallocBinned3.cpp and Public/HAL/MallocBinnedCommon.h unless stated.
namespace bw::memory {
namespace {
using VM = platform::AllocatorVirtualMemoryBlock;
constexpr auto unavailable = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t no_next = 4096U;
constexpr std::uint32_t large_unassigned = 0x39431234U;
constexpr std::uint32_t large_assigned = 0x17ea5678U;
// Extracted source Align, used for metadata and committed page extents.
std::size_t round_up(std::size_t bytes, std::size_t alignment) {
  return (bytes + alignment - 1U) & ~(alignment - 1U);
}
// Binned3.cpp:103-163 FFreeBlock: an eight-byte header overlays a free run. Its
// head is returned last; client data never shares a live allocation header.
struct FreeRun {
  std::uint32_t size_shifted : 15;
  std::uint32_t pool : 7;
  std::uint32_t canary : 8;
  std::uint32_t count : 12;
  std::uint32_t next : 13;
};
static_assert(sizeof(FreeRun) == 8U);
// Binned3.cpp:443-460: enforce process singleton before reserving virtual memory.
// Extracted to retain this ordering during member construction.
VM reserve_small_ranges() {
  [[maybe_unused]] static bool constructed = false;
  BW_ALLOCATOR_CHECK(!constructed);
  constructed = true;
  return VM::allocate_virtual(binned_class_count * binned_reservation_per_class,
                              VM::commit_alignment());
}
} // namespace
BinnedRecycler BinnedAllocator::recycler_;

// Binned3.cpp:443-549 constructor: reserve class address ranges first, establish
// native-page geometry and one packed region for pointer tables and bit trees.
BinnedAllocator::BinnedAllocator()
    : small_reservation_(reserve_small_ranges()),
      sizes_(static_cast<std::uint32_t>(VM::commit_alignment())),
      page_size_(static_cast<std::uint32_t>(VM::commit_alignment())),
      small_infos_per_page_(page_size_ / sizeof(SmallBlockInfo)),
      large_infos_per_page_(page_size_ / sizeof(LargeAllocationInfo)) {
  std::size_t required = 0U;
  for (std::uint32_t index = 0; index < binned_class_count; ++index) {
    auto& pool = pools_[index];
    pool.bin_size = sizes_.at(index).bytes;
    pool.pages_per_block = sizes_.at(index).pages_per_block;
    pool.block_size = pool.pages_per_block * page_size_;
    const auto blocks = static_cast<std::uint32_t>(binned_reservation_per_class / pool.block_size);
    // Source pointer-table rounding includes the source's multiply-before-divide.
    required += round_up(sizeof(SmallBlockInfo*) * (blocks + small_infos_per_page_ - 1U) /
                             small_infos_per_page_,
                         platform::allocator_cache_line_size);
    required += 2U * round_up(BinnedBitTree::memory_requirement(blocks),
                              platform::allocator_cache_line_size);
  }
  required = round_up(required, page_size_);
  auto* metadata = static_cast<std::byte*>(allocate_metadata(required));
  std::memset(metadata, 0, required);
  for (auto& pool : pools_) {
    const auto blocks = static_cast<std::uint32_t>(binned_reservation_per_class / pool.block_size);
    const auto pointers = round_up(sizeof(SmallBlockInfo*) * (blocks + small_infos_per_page_ - 1U) /
                                       small_infos_per_page_,
                                   platform::allocator_cache_line_size);
    pool.block_info_pages = reinterpret_cast<SmallBlockInfo**>(metadata);
    metadata += pointers;
    const auto tree_bytes = static_cast<std::uint32_t>(
        round_up(BinnedBitTree::memory_requirement(blocks), platform::allocator_cache_line_size));
    pool.allocated_blocks.initialize(blocks, metadata, tree_bytes, false);
    metadata += tree_bytes;
    pool.exhausted_blocks.initialize(blocks, metadata, tree_bytes, true);
    metadata += tree_bytes;
  }
  initialize_large_metadata();
}
// Binned3.cpp:551-553: allocator/pool storage is process lifetime, no teardown.
BinnedAllocator::~BinnedAllocator() = default;

// Binned3.h:182-223 PoolIndexFromPtr/PoolBasePtr/BlockPointerFromIndecies:
// unsigned subtraction classifies external pointers without per-slot headers.
std::uint64_t BinnedAllocator::pool_index(const void* pointer) const {
  return (reinterpret_cast<std::uintptr_t>(pointer) -
          reinterpret_cast<std::uintptr_t>(small_reservation_.virtual_pointer())) /
         binned_reservation_per_class;
}
std::byte* BinnedAllocator::pool_base(std::uint32_t pool) const {
  return static_cast<std::byte*>(small_reservation_.virtual_pointer()) +
         pool * binned_reservation_per_class;
}
std::byte* BinnedAllocator::block_address(std::uint32_t pool, std::uint32_t block) const {
  return pool_base(pool) + static_cast<std::size_t>(block) * pools_[pool].block_size;
}

// Binned3.cpp:569-589 Allocate/FreeMetaDataMemory: direct VM bypasses allocator
// recursion. Descriptor aliases are rebuilt only by the explicit owner on release.
void* BinnedAllocator::allocate_metadata(std::size_t bytes) {
  auto block = VM::allocate_virtual(bytes);
  block.commit(0U, round_up(bytes, VM::commit_alignment()));
  return block.virtual_pointer();
}
void BinnedAllocator::release_metadata(void* pointer, std::size_t bytes) {
  VM block(pointer, static_cast<std::uint32_t>(round_up(bytes, VM::virtual_size_alignment()) /
                                               VM::virtual_size_alignment()));
  block.free_virtual();
}

// Binned3.cpp:291-315 GetOrCreatePoolInfoSmall: lazily allocate a page of block
// records. Existing assigned blocks remain assigned; recycled ones re-enter use.
// Source class index and block index remain separate documented coordinates.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
BinnedAllocator::SmallBlockInfo& BinnedAllocator::block_info(std::uint32_t index,
                                                             std::uint32_t block) {
  auto& pool = pools_[index];
  auto*& page = pool.block_info_pages[block / small_infos_per_page_];
  if (page == nullptr) {
    page = static_cast<SmallBlockInfo*>(allocate_metadata(page_size_));
    for (std::uint32_t i = 0; i < small_infos_per_page_; ++i) {
      ::new (page + i) SmallBlockInfo{3U, 0U, 1U, 0U};
    }
  }
  auto& result = page[block % small_infos_per_page_];
  BW_ALLOCATOR_REQUIRE(block >= pool.ever_used_blocks ? result.canary == 3U
                                                      : result.canary == 3U || result.canary == 1U);
  pool.ever_used_blocks = std::max(pool.ever_used_blocks, block + 1U);
  result.canary = 1U;
  return result;
}

// Binned3.cpp:433-441 GetFrontPool: exhaustion summary selects a usable block.
BinnedAllocator::SmallBlockInfo* BinnedAllocator::available_block(std::uint32_t pool,
                                                                  std::uint32_t& block) {
  block = pools_[pool].exhausted_blocks.next_available();
  return block == unavailable ? nullptr : &block_info(pool, block);
}

// Binned3.cpp:396-430 PushNewPoolToFront: reserve one unused block, commit it,
// create one descending free run, and expose it in the non-exhausted tree.
BinnedAllocator::SmallBlockInfo* BinnedAllocator::commit_block(std::uint32_t index,
                                                               std::uint32_t& block) {
  auto& pool = pools_[index];
  block = pool.allocated_blocks.allocate();
  if (block == unavailable) {
    return nullptr;
  }
  auto* address = block_address(index, block);
  small_reservation_.commit_by_pointer(address, pool.block_size);
  pool.unused_offset = std::max(
      pool.unused_offset, static_cast<std::uint64_t>(address + pool.block_size - pool_base(index)));
  ::new (address)
      FreeRun{pool.bin_size >> 3U, index, 0xe7U, pool.block_size / pool.bin_size, no_next};
  auto& info = block_info(index, block);
  info.taken = 0U;
  info.no_free_index = 0U;
  info.first_free_index = 0U;
  pool.exhausted_blocks.release(block);
  return &info;
}

// Binned3.cpp:207-230 FPoolInfoSmall::AllocateBin: consume a descending run;
// move to its successor only when the run becomes empty.
void* BinnedAllocator::allocate_bin(std::uint32_t index, std::uint32_t block,
                                    SmallBlockInfo& info) {
  BW_ALLOCATOR_REQUIRE(info.canary == 1U);
  BW_ALLOCATOR_CHECK(info.no_free_index == 0U);
  const auto size = pools_[index].bin_size;
  auto* run = reinterpret_cast<FreeRun*>(block_address(index, block) +
                                         static_cast<std::size_t>(size) * info.first_free_index);
  BW_ALLOCATOR_REQUIRE(run->canary == 0xe7U);
  ++info.taken;
  --run->count;
  void* result = reinterpret_cast<std::byte*>(run) + static_cast<std::size_t>(run->count) * size;
  if (run->count == 0U) {
    if (run->next == no_next) {
      info.first_free_index = 0U;
      info.no_free_index = 1U;
    } else {
      info.first_free_index = run->next;
    }
  }
  return result;
}

// Binned3.cpp:319-386 FreeBundles: one class lock protects free chains/counts.
// Save next before overwriting each client slot; discard a block only at taken0.
void BinnedAllocator::release_bundles(BinnedBundleNode* bundle, std::uint32_t index) {
  auto& pool = pools_[index];
  const std::lock_guard lock(pool.mutex);
  while (bundle != nullptr) {
    auto* next = bundle->next();
    const auto offset = reinterpret_cast<std::byte*>(bundle) - pool_base(index);
    const auto block =
        static_cast<std::uint32_t>(static_cast<std::size_t>(offset) / pool.block_size);
    auto* page = pool.block_info_pages[block / small_infos_per_page_];
    BW_ALLOCATOR_REQUIRE(page != nullptr);
    auto& info = page[block % small_infos_per_page_];
    BW_ALLOCATOR_REQUIRE(info.canary == 1U);
    const bool exhausted = info.no_free_index != 0U;
    ::new (bundle)
        FreeRun{pool.bin_size >> 3U, index, 0xe7U, 1U, exhausted ? no_next : info.first_free_index};
    info.first_free_index = static_cast<std::uint32_t>(
        (reinterpret_cast<std::byte*>(bundle) - block_address(index, block)) / pool.bin_size);
    info.no_free_index = 0U;
    BW_ALLOCATOR_CHECK(info.taken != 0U);
    --info.taken;
    if (info.taken == 0U) {
      info.canary = 3U;
      pool.allocated_blocks.release(block);
      if (!exhausted) {
        pool.exhausted_blocks.allocate(block);
      }
      small_reservation_.decommit_by_pointer(block_address(index, block), pool.block_size);
    } else if (exhausted) {
      pool.exhausted_blocks.release(block);
    }
    bundle = next;
  }
}

// Common.h:207-263 FPtrToPoolMapping::Init;943-952 AllocateHashBuckets: hash
// geometry uses physical-memory hint, with circular collision lists per bucket.
void BinnedAllocator::initialize_large_metadata() {
  const auto& constants = platform::allocator_constants();
  address_base_ = constants.address_start;
  if (address_base_ == reinterpret_cast<std::uintptr_t>(small_reservation_.virtual_pointer())) {
    address_base_ += binned_class_count * binned_reservation_per_class;
  }
  page_bit_shift_ = static_cast<std::uint64_t>(std::bit_width(page_size_ - 1U));
  hash_key_shift_ =
      page_bit_shift_ + static_cast<std::uint64_t>(std::bit_width(large_infos_per_page_ - 1U));
  pool_mask_ = large_infos_per_page_ - 1U;
  maximum_hash_buckets_ = std::bit_ceil(constants.address_limit - address_base_) >> hash_key_shift_;
  hash_buckets_ =
      static_cast<HashBucket*>(allocate_metadata(maximum_hash_buckets_ * sizeof(HashBucket)));
  for (std::uint64_t i = 0; i < maximum_hash_buckets_; ++i) {
    auto* bucket = ::new (hash_buckets_ + i) HashBucket{};
    bucket->next = bucket;
    bucket->previous = bucket;
  }
}

// Common.h:739-760 FindPoolInfo: locate pointer's collision key and page index.
BinnedAllocator::LargeAllocationInfo* BinnedAllocator::find_large(void* pointer) {
  const auto relative = reinterpret_cast<std::uintptr_t>(pointer) - address_base_;
  const auto key = relative >> hash_key_shift_;
  auto* first = hash_buckets_ + (key & (maximum_hash_buckets_ - 1U));
  auto* bucket = first;
  do {
    if (bucket->key == key && bucket->first_pool != nullptr) {
      return bucket->first_pool + ((relative >> page_bit_shift_) & pool_mask_);
    }
    bucket = bucket->next;
  } while (bucket != first);
  return nullptr;
}

// Common.h:645-737 GetOrCreatePoolInfo: metadata pages and collision nodes come
// directly from VM. A bucket's metadata stays alive when individual clients free.
BinnedAllocator::LargeAllocationInfo& BinnedAllocator::create_large(void* pointer) {
  const auto relative = reinterpret_cast<std::uintptr_t>(pointer) - address_base_;
  const auto key = relative >> hash_key_shift_;
  auto* first = hash_buckets_ + (key & (maximum_hash_buckets_ - 1U));
  auto* bucket = first;
  do {
    if (bucket->first_pool == nullptr || bucket->key == key) {
      break;
    }
    bucket = bucket->next;
  } while (bucket != first);
  if (bucket->first_pool != nullptr && bucket->key != key) {
    // Source collision-page freelist is circular, and nodes are unlinked/relinked.
    if (free_hash_buckets_ == nullptr) {
      auto* page = static_cast<HashBucket*>(allocate_metadata(page_size_));
      const auto count = page_size_ / sizeof(HashBucket);
      for (std::size_t i = 0; i < count; ++i) {
        ::new (page + i) HashBucket{};
        page[i].previous = page + (i + count - 1U) % count;
        page[i].next = page + (i + 1U) % count;
      }
      free_hash_buckets_ = page;
    }
    bucket = free_hash_buckets_;
    free_hash_buckets_ = bucket->next == bucket ? nullptr : bucket->next;
    bucket->previous->next = bucket->next;
    bucket->next->previous = bucket->previous;
    bucket->next = first->next;
    bucket->previous = first;
    first->next->previous = bucket;
    first->next = bucket;
  }
  if (bucket->first_pool == nullptr) {
    bucket->first_pool = static_cast<LargeAllocationInfo*>(allocate_metadata(page_size_));
    for (std::uint32_t i = 0; i < large_infos_per_page_; ++i) {
      ::new (bucket->first_pool + i) LargeAllocationInfo{large_unassigned, 0U, 0U, 0U};
    }
  }
  bucket->key = key;
  auto& info = bucket->first_pool[(relative >> page_bit_shift_) & pool_mask_];
  BW_ALLOCATOR_REQUIRE(info.canary == large_unassigned || info.canary == large_assigned);
  info.canary = large_assigned;
  return info;
}

// Binned3.cpp:597-766 Malloc: cache/recycler first, class-locked free runs next;
// on reservation exhaustion promote to the next source class, never libc malloc.
void* BinnedAllocator::allocate(std::size_t bytes, std::uint32_t alignment) {
  BW_ALLOCATOR_CHECK((alignment & (alignment - 1U)) == 0U);
  bool pooled = bytes <= binned_small_limit;
  if (pooled && alignment > 16U) {
    pooled = sizes_.promote(bytes, alignment);
  }
  if (!pooled) {
    return allocate_without_cache(bytes, alignment);
  }
  const auto index = sizes_.index_for(bytes);
  auto* cache = BinnedThreadCache::current_if_owned();
  if (cache != nullptr && cache->obtain_partial(index, recycler_)) {
    if (void* result = cache->allocate(index)) {
      return result;
    }
  }
  auto& pool = pools_[index];
  const std::lock_guard lock(pool.mutex);
  std::uint32_t block = unavailable;
  auto* info = available_block(index, block);
  if (info == nullptr) {
    info = commit_block(index, block);
  }
  if (info == nullptr) {
    return allocate(index + 1U < binned_class_count ? pools_[index + 1U].bin_size
                                                    : binned_small_limit + 1U,
                    alignment);
  }
  void* result = allocate_bin(index, block, *info);
  // Binned3.cpp:679-698: prefill up to32 extra bins while a free run remains.
  if (cache != nullptr) {
    for (std::uint32_t extra = 0; extra < 32U && info->no_free_index == 0U; ++extra) {
      if (!cache->release(result, index, pool.bin_size)) {
        break;
      }
      result = allocate_bin(index, block, *info);
    }
  }
  if (info->no_free_index != 0U) {
    pool.exhausted_blocks.allocate(block);
  }
  return result;
}

// Binned3.cpp:768-835 MallocNoTLS: identical class/large routing without cache
// prefill. Mac large path (714-752) allocates/commits native pages directly.
void* BinnedAllocator::allocate_without_cache(std::size_t bytes, std::uint32_t alignment) {
  BW_ALLOCATOR_CHECK((alignment & (alignment - 1U)) == 0U);
  bool pooled = bytes <= binned_small_limit;
  if (pooled && alignment > 16U) {
    pooled = sizes_.promote(bytes, alignment);
  }
  if (pooled) {
    const auto index = sizes_.index_for(bytes);
    auto& pool = pools_[index];
    const std::lock_guard lock(pool.mutex);
    std::uint32_t block = unavailable;
    auto* info = available_block(index, block);
    if (info == nullptr) {
      info = commit_block(index, block);
    }
    if (info == nullptr) {
      return allocate_without_cache(index + 1U < binned_class_count ? pools_[index + 1U].bin_size
                                                                    : binned_small_limit + 1U,
                                    alignment);
    }
    void* result = allocate_bin(index, block, *info);
    if (info->no_free_index != 0U) {
      pool.exhausted_blocks.allocate(block);
    }
    return result;
  }
  alignment = std::max(alignment, 16U);
  const auto committed = round_up(bytes, page_size_);
  auto block = VM::allocate_virtual(committed, alignment);
  block.commit(0U, committed);
  void* result = block.virtual_pointer();
  const std::lock_guard lock(external_mutex_);
  auto& info = create_large(result);
  BW_ALLOCATOR_CHECK(bytes > 0U && bytes <= committed);
  info.requested_bytes = static_cast<std::uint32_t>(bytes);
  info.committed_bytes = static_cast<std::uint32_t>(committed);
  info.vm_pages = block.actual_size_in_pages();
  BW_ALLOCATOR_REQUIRE(reinterpret_cast<std::uintptr_t>(result) % alignment == 0U);
  return result;
}

// Binned3.cpp:837-908 Realloc: keep current bin only within its class interval;
// keep large mapping while page extent is unchanged, else allocate-copy-free.
void* BinnedAllocator::reallocate(void* pointer, std::size_t bytes, std::uint32_t alignment) {
  if (bytes == 0U) {
    release_external(pointer, pool_index(pointer));
    return nullptr;
  }
  BW_ALLOCATOR_CHECK((alignment & (alignment - 1U)) == 0U);
  BW_ALLOCATOR_CHECK(alignment <= page_size_);
  if (pointer == nullptr) {
    return allocate(bytes, alignment);
  }
  const auto index = pool_index(pointer);
  std::size_t old_bytes = 0U;
  if (index < binned_class_count) {
    old_bytes = pools_[index].bin_size;
    if (bytes <= old_bytes && (alignment == 0U || old_bytes % alignment == 0U) &&
        (index == 0U || bytes > pools_[index - 1U].bin_size)) {
      return pointer;
    }
  } else {
    const std::lock_guard lock(external_mutex_);
    auto* info = find_large(pointer);
    BW_ALLOCATOR_REQUIRE(info != nullptr && info->canary == large_assigned);
    old_bytes = info->requested_bytes;
    const bool move = bytes > info->committed_bytes ||
                      (bytes <= binned_small_limit && alignment <= 16U) ||
                      round_up(bytes, page_size_) < info->committed_bytes;
    if (!move) {
      info->requested_bytes = static_cast<std::uint32_t>(bytes);
      return pointer;
    }
  }
  void* result = allocate(bytes, alignment);
  std::memcpy(result, pointer, std::min(bytes, old_bytes));
  release_external(pointer, pool_index(pointer));
  return result;
}

// Binned3.h Free and .cpp:910-1003 FreeExternal: current thread owns its free
// cache, independently of which thread allocated the client pointer.
void BinnedAllocator::release(void* pointer) { release_external(pointer, pool_index(pointer)); }
void BinnedAllocator::release_external(void* pointer, std::uint64_t index) {
  if (index < binned_class_count) {
    const auto pool = static_cast<std::uint32_t>(index);
    auto* cache = BinnedThreadCache::current_if_owned();
    BinnedBundleNode* returned = nullptr;
    if (cache != nullptr) {
      returned = cache->recycle_full(pool, recycler_);
      BW_ALLOCATOR_VERIFY(cache->release(pointer, pool, pools_[pool].bin_size));
    } else {
      returned = ::new (pointer) BinnedBundleNode{};
      returned->set_next(nullptr);
    }
    if (returned != nullptr) {
      release_bundles(returned, pool);
    }
    return;
  }
  if (pointer == nullptr) {
    return;
  }
  LargeAllocationInfo* info = nullptr;
  {
    const std::lock_guard lock(external_mutex_);
    info = find_large(pointer);
  }
  BW_ALLOCATOR_REQUIRE(info != nullptr && info->canary == large_assigned);
  const auto pages = info->vm_pages;
  info->canary = large_unassigned;
  VM block(pointer, pages);
  block.free_virtual();
}

// Binned3.cpp:1005-1029 FreeNoTLS: one-node bundle returns immediately to pool.
void BinnedAllocator::release_without_cache(void* pointer) {
  const auto index = pool_index(pointer);
  if (index < binned_class_count) {
    auto* node = ::new (pointer) BinnedBundleNode{};
    release_bundles(node, static_cast<std::uint32_t>(index));
  } else {
    release_external(pointer, index);
  }
}

// Common.h:823-855 QuantizeSizeCommon: immutable class policy already extracted.
std::size_t BinnedAllocator::quantize(std::size_t bytes, std::uint32_t alignment) const {
  return sizes_.quantize(bytes, alignment);
}
// Common.h:896-923 GetAllocationSizeExternal: small bin capacity; large committed
// byte extent. Null is an unsuccessful query, not an allocation failure.
bool BinnedAllocator::allocation_size(void* pointer, std::size_t& bytes) {
  const auto index = pool_index(pointer);
  if (index < binned_class_count) {
    bytes = pools_[index].bin_size;
    return true;
  }
  if (pointer == nullptr) {
    return false;
  }
  const std::lock_guard lock(external_mutex_);
  auto* info = find_large(pointer);
  BW_ALLOCATOR_REQUIRE(info != nullptr && info->canary == large_assigned);
  bytes = info->committed_bytes;
  return true;
}
// Binned3.cpp:592-595 IsInternallyThreadSafe: per-class and metadata locks suffice.
bool BinnedAllocator::internally_thread_safe() const { return true; }

// Common.h:762-772,958-971: registry has a distinct lock and dynamic pointer
// storage. Source TArray Add growth is ContainerAllocationPolicies.h:170-219;
// Remove/RemoveAll Array.h:3343-3410 keeps capacity and stable entry order.
struct BinnedAllocator::CacheRegistry {
  platform::AllocatorRecursiveMutex mutex;
  BinnedThreadCache** entries{};
  std::int32_t count{};
  std::int32_t capacity{};
};
BinnedAllocator::CacheRegistry& BinnedAllocator::cache_registry() {
  static CacheRegistry registry;
  return registry;
}
// RegisterThreadFreeBlockLists: published/locked TLS precedes this possible
// allocation; selected Binned3 owns registry storage just as source GMalloc does.
void BinnedAllocator::register_cache(BinnedThreadCache& cache) {
  auto& registry = cache_registry();
  const std::lock_guard lock(registry.mutex);
  if (registry.count == registry.capacity) {
    const auto needed = static_cast<std::size_t>(registry.count) + 1U;
    const auto growth =
        registry.capacity == 0 ? std::max(needed, std::size_t{4}) : needed + 3U * needed / 8U + 16U;
    const auto capacity =
        quantize(growth * sizeof(BinnedThreadCache*)) / sizeof(BinnedThreadCache*);
    BW_ALLOCATOR_REQUIRE(capacity <=
                         static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()));
    registry.entries = static_cast<BinnedThreadCache**>(
        reallocate(static_cast<void*>(registry.entries), capacity * sizeof(BinnedThreadCache*)));
    registry.capacity = static_cast<std::int32_t>(capacity);
  }
  registry.entries[registry.count++] = &cache;
}
// UnregisterThreadFreeBlockLists: source Remove removes matching runs preserving
// other pointers. Unique registration means one match; trivial relocation memmove.
void BinnedAllocator::unregister_cache(BinnedThreadCache& cache) {
  auto& registry = cache_registry();
  const std::lock_guard lock(registry.mutex);
  for (std::int32_t index = 0; index < registry.count; ++index) {
    if (registry.entries[index] == &cache) {
      std::memmove(static_cast<void*>(registry.entries + index),
                   static_cast<const void*>(registry.entries + index + 1),
                   static_cast<std::size_t>(registry.count - index - 1) *
                       sizeof(BinnedThreadCache*));
      --registry.count;
      return;
    }
  }
}
// Common.h:774-821 Setup/Clear/MarkTLSCachesAsUsed/Unused: explicit owner hooks.
void BinnedAllocator::setup_current_thread_cache() { BinnedThreadCache::setup(*this); }
void BinnedAllocator::clear_current_thread_cache() { BinnedThreadCache::clear(*this); }
void BinnedAllocator::mark_current_thread_cache_used() { BinnedThreadCache::mark_used(); }
void BinnedAllocator::mark_current_thread_cache_unused() { BinnedThreadCache::mark_unused(*this); }
// CommonUtils.h:103-140 FlushCurrentThreadCacheInternal/TrimThreadFreeBlockLists:
// under current owner lock, return all detached class bundles once per requested
// epoch (or force). No other thread reads the owner's mutable lists here.
void BinnedAllocator::flush_current_thread_cache(bool new_epoch_only) {
  auto* cache = BinnedThreadCache::current_if_owned();
  if (cache == nullptr) {
    return;
  }
  const bool changed_epoch = cache->update_epoch(trim_epoch_.load(std::memory_order_relaxed));
  if (new_epoch_only && !changed_epoch) {
    return;
  }
  for (std::uint32_t pool = 0; pool < binned_class_count; ++pool) {
    if (auto* bundle = cache->detach_bundles(pool)) {
      release_bundles(bundle, pool);
    }
  }
}
// CommonUtils.h:142-189 Trim / TaskGraph.cpp:1913-2009 desktop false,false:
// advance trim epoch, execute game-local callback, enqueue render/RHI callbacks.
// Named owners process asynchronously; source does not await named completions.
void BinnedAllocator::trim(bool trim_thread_caches) {
  if (!trim_thread_caches) {
    return;
  }
  trim_epoch_.fetch_add(1U, std::memory_order_relaxed);
  tasks::NamedThreadDispatch::instance().broadcast(
      {[](void* context, tasks::NamedThread) noexcept {
         static_cast<BinnedAllocator*>(context)->flush_current_thread_cache(true);
       },
       this});
}
} // namespace bw::memory
