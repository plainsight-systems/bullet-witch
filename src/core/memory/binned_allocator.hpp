#ifndef BW_CORE_MEMORY_BINNED_ALLOCATOR_HPP
#define BW_CORE_MEMORY_BINNED_ALLOCATOR_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "core/memory/allocator.hpp"
#include "core/memory/binned_bit_tree.hpp"
#include "core/memory/binned_size_classes.hpp"
#include "core/memory/binned_thread_cache.hpp"
#include "platform/allocator_platform.hpp"
#include "platform/allocator_sync.hpp"

// Core CP.2: per-class, external-metadata and registry locks have distinct owners.
// Core R.15: explicit paired metadata lifetime; caller destroys client objects.
// Performance MEM.2/MEM.5: source size classes and intrusive free slots, direct VM
// for large allocations. All storage is owned by the process-lifetime allocator.
namespace bw::memory {

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// HAL/MallocBinned3.cpp:443-553. One process-lifetime allocator; construction is
// serialized by facade bootstrap. Destruction does not tear down global pools.
class BinnedAllocator final : public Allocator {
public:
  BinnedAllocator();
  ~BinnedAllocator() override;
  // .cpp:597-766: TLS/recycler first, then class-locked blocks; large -> native VM.
  [[nodiscard]] void* allocate(std::size_t bytes, std::uint32_t alignment = 0) override;
  // .cpp:768-835: bypass TLS; use identical class/large semantics.
  [[nodiscard]] void* allocate_without_cache(std::size_t bytes, std::uint32_t alignment = 0);
  // .cpp:837-908: retain same class/page extent or allocate-copy-free; zero frees.
  [[nodiscard]] void* reallocate(void* pointer, std::size_t bytes,
                                 std::uint32_t alignment = 0) override;
  // .cpp:910-1029: free on current thread's cache; no allocating-thread affinity.
  void release(void* pointer) override;
  void release_without_cache(void* pointer);
  // MallocBinnedCommon.h:823-855,896-923: size-class capacity or OS committed size.
  [[nodiscard]] std::size_t quantize(std::size_t bytes, std::uint32_t alignment = 0) const override;
  [[nodiscard]] bool allocation_size(void* pointer, std::size_t& bytes) override;
  // MallocBinned3.cpp:592-595: internal locking, no outer proxy mutex.
  [[nodiscard]] bool internally_thread_safe() const override;

  // MallocBinnedCommon.h:774-821: explicit owner-thread lifecycle hooks.
  void setup_current_thread_cache() override;
  void mark_current_thread_cache_used() override;
  void mark_current_thread_cache_unused() override;
  void clear_current_thread_cache() override;
  // MallocBinnedCommonUtils.h:120-140: flush this owned cache, optionally by epoch.
  void flush_current_thread_cache(bool new_epoch_only = false);
  // MallocBinnedCommonUtils.h:142-189: game-local flush and asynchronous named-
  // owner broadcasts; source desktop path does not wait for named completion.
  void trim(bool trim_thread_caches) override;

private:
  friend class BinnedThreadCache;

  // MallocBinned3.cpp:166-231: 4-byte per-block count/canary/free-chain metadata.
  struct SmallBlockInfo {
    std::uint32_t canary : 2;
    std::uint32_t taken : 15;
    std::uint32_t no_free_index : 1;
    std::uint32_t first_free_index : 14;
  };
  static_assert(sizeof(SmallBlockInfo) == 4);
  // MallocBinned3.h:89-120: large mapping metadata lives outside the mapping.
  struct LargeAllocationInfo {
    std::uint32_t canary{};
    std::uint32_t requested_bytes{};
    std::uint32_t vm_pages{};
    std::uint32_t committed_bytes{};
  };
  struct Pool {
    std::uint32_t bin_size{};
    std::uint32_t block_size{};
    std::uint32_t pages_per_block{};
    std::uint32_t ever_used_blocks{};
    BinnedBitTree allocated_blocks;
    BinnedBitTree exhausted_blocks;
    SmallBlockInfo** block_info_pages{};
    std::uint64_t unused_offset{};
    platform::AllocatorRecursiveMutex mutex;
  };
  // MallocBinnedCommon.h:456-487: circular collision lists, allocated through VM.
  struct HashBucket {
    std::uintptr_t key{};
    LargeAllocationInfo* first_pool{};
    HashBucket* previous{};
    HashBucket* next{};
  };
  // Registry is the source dynamic pointer array under its own mutex. Container
  // growth is a required dependency, not a guessed fixed-capacity thread table.
  struct CacheRegistry;

  // MallocBinned3.h:182-223: unsigned address offset identifies class/block.
  [[nodiscard]] std::uint64_t pool_index(const void* pointer) const;
  [[nodiscard]] std::byte* pool_base(std::uint32_t pool) const;
  [[nodiscard]] std::byte* block_address(std::uint32_t pool, std::uint32_t block) const;
  // .cpp:291-315,396-441: lazily obtain info, select or commit available block.
  [[nodiscard]] SmallBlockInfo& block_info(std::uint32_t pool, std::uint32_t block);
  [[nodiscard]] SmallBlockInfo* available_block(std::uint32_t pool, std::uint32_t& block);
  [[nodiscard]] SmallBlockInfo* commit_block(std::uint32_t pool, std::uint32_t& block);
  // .cpp:207-230,319-386: free chain allocation and locked bundle return/decommit.
  [[nodiscard]] void* allocate_bin(std::uint32_t pool, std::uint32_t block, SmallBlockInfo& info);
  void release_bundles(BinnedBundleNode* bundle, std::uint32_t pool);
  void release_external(void* pointer, std::uint64_t pool);
  // MallocBinnedCommon.h:645-760,943-952: hash geometry/collision metadata.
  void initialize_large_metadata();
  [[nodiscard]] LargeAllocationInfo* find_large(void* pointer);
  [[nodiscard]] LargeAllocationInfo& create_large(void* pointer);
  // MallocBinned3.cpp:569-589: direct native VM, never recursively general malloc.
  [[nodiscard]] static void* allocate_metadata(std::size_t bytes);
  static void release_metadata(void* pointer, std::size_t bytes);
  // MallocBinnedCommon.h:762-772,958-971: separately locked registration ownership.
  static CacheRegistry& cache_registry();
  void register_cache(BinnedThreadCache& cache);
  void unregister_cache(BinnedThreadCache& cache);

  platform::AllocatorVirtualMemoryBlock small_reservation_;
  BinnedSizeClasses sizes_;
  std::array<Pool, binned_class_count> pools_;
  std::uint32_t page_size_{};
  std::uint32_t small_infos_per_page_{};
  std::uint32_t large_infos_per_page_{};
  std::uint64_t hash_key_shift_{};
  std::uint64_t page_bit_shift_{};
  std::uint64_t pool_mask_{};
  std::uint64_t maximum_hash_buckets_{};
  std::uint64_t address_base_{};
  HashBucket* hash_buckets_{};
  HashBucket* free_hash_buckets_{};
  platform::AllocatorRecursiveMutex external_mutex_;
  std::atomic<std::uint64_t> trim_epoch_{0};
  static BinnedRecycler recycler_;
};
} // namespace bw::memory

#endif
