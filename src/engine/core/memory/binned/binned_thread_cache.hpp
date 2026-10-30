#ifndef BW_CORE_MEMORY_BINNED_THREAD_CACHE_HPP
#define BW_CORE_MEMORY_BINNED_THREAD_CACHE_HPP

#include <array>
#include <atomic>
#include <cstdint>

#include "binned/binned_size_classes.hpp"
#include "bw/concurrency/word_mutex.hpp"
#include "bw/platform/memory/allocator_sync.hpp"

// Core CP.2: local bundles have one locked owner; recycler CAS transfers bundles.
// Performance MEM.2/MEM.5: released slots carry links, split by source size class.
namespace bw::memory {
class BinnedAllocator;

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// MallocBinnedCommon.h:186-202. Link/count fit the source 8-byte smallest slot.
struct BinnedBundleNode {
  std::uint64_t next_address : 48;
  std::uint64_t count : 8;
  std::uint64_t reserved : 8;
  void set_next(BinnedBundleNode* node);
  [[nodiscard]] BinnedBundleNode* next() const;
};
static_assert(sizeof(BinnedBundleNode) == 8);

// MallocBinnedCommonUtils.h:45-95. Each slot transfers one whole bundle, never
// exposing mutable cache lists to another owner. Source eight-slot bound.
class BinnedRecycler final {
public:
  BinnedRecycler();
  [[nodiscard]] bool push(std::uint32_t pool, BinnedBundleNode* bundle);
  [[nodiscard]] BinnedBundleNode* pop(std::uint32_t pool);

private:
  struct alignas(platform::allocator_cache_line_size) Slots {
    std::array<std::atomic<BinnedBundleNode*>, 8> bundles{};
  };
  std::array<Slots, binned_class_count> pools_{};
};

// MallocBinnedCommon.h:265-405. No mutex inside: caller owns the cache lock.
class BinnedFreeList final {
public:
  BinnedFreeList();
  [[nodiscard]] bool push(void* slot, std::uint32_t bin_size);
  [[nodiscard]] void* pop();
  [[nodiscard]] BinnedBundleNode* recycle_full(std::uint32_t pool, BinnedRecycler& recycler);
  [[nodiscard]] bool obtain_partial(std::uint32_t pool, BinnedRecycler& recycler);
  [[nodiscard]] BinnedBundleNode* detach_bundles();

private:
  struct Bundle {
    BinnedBundleNode* head{};
    std::uint32_t count{};
  };
  Bundle partial_{};
  Bundle full_{};
};

// MallocBinnedCommon.h:500-643,774-821. Setup/clear are explicit thread lifetime
// operations; no automatic thread_local object destructor replaces engine hooks.
class BinnedThreadCache final {
public:
  BinnedThreadCache();
  [[nodiscard]] static BinnedThreadCache* current_if_owned();
  static void setup(BinnedAllocator& allocator);
  static void clear(BinnedAllocator& allocator);
  static void mark_used();
  static void mark_unused(BinnedAllocator& allocator);
  [[nodiscard]] void* allocate(std::uint32_t pool);
  [[nodiscard]] bool release(void* slot, std::uint32_t pool, std::uint32_t size);
  [[nodiscard]] BinnedBundleNode* recycle_full(std::uint32_t pool, BinnedRecycler& recycler);
  [[nodiscard]] bool obtain_partial(std::uint32_t pool, BinnedRecycler& recycler);
  [[nodiscard]] BinnedBundleNode* detach_bundles(std::uint32_t pool);
  [[nodiscard]] bool update_epoch(std::uint64_t epoch);

private:
  friend class BinnedAllocator;
  static thread_local BinnedThreadCache* current_;
  concurrency::WordMutex mutex_;
  std::uint64_t trim_epoch_{};
  std::array<BinnedFreeList, binned_class_count> lists_{};
  bool locked_by_owner_{};
};
} // namespace bw::memory

#endif
