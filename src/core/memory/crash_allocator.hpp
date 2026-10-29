#ifndef BW_CORE_MEMORY_CRASH_ALLOCATOR_HPP
#define BW_CORE_MEMORY_CRASH_ALLOCATOR_HPP

#include "core/memory/allocator.hpp"
#include "platform/allocator_sync.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

// Core R.1: singleton owns reserved regions for process lifetime; source crash
// activation deliberately retains its lock. Performance MEM.2: reserve storage
// up front, with source scan/promotion instead of a replacement free-list policy.
namespace bw::memory {
class BinnedAllocator;

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// GenericPlatformMallocCrash.h:43-177; .cpp:74-671. Mac pooled variant.
// Only the activating thread may allocate. Other threads suspend on allocation
// and skip frees, following the source default, not a general-purpose heap.
class CrashAllocator final : public Allocator {
public:
  static CrashAllocator& get(BinnedAllocator* previous = nullptr);
  static bool is_active();
  void activate();
  ~CrashAllocator() override;
  [[nodiscard]] void* allocate(std::size_t bytes, std::uint32_t alignment = 0) override;
  [[nodiscard]] void* reallocate(void* pointer, std::size_t bytes,
                                 std::uint32_t alignment = 0) override;
  void release(void* pointer) override;
  [[nodiscard]] bool allocation_size(void* pointer, std::size_t& bytes) override;
  [[nodiscard]] bool internally_thread_safe() const override;
  [[nodiscard]] std::size_t quantize(std::size_t bytes, std::uint32_t alignment = 0) const override;
  void trim(bool trim_thread_caches) override;
  void setup_current_thread_cache() override;
  void mark_current_thread_cache_used() override;
  void mark_current_thread_cache_unused() override;
  void clear_current_thread_cache() override;

private:
  struct Slot {
    std::uint64_t bytes{};
    std::byte* pointer{};
  };
  struct Pool {
    std::uint32_t used{};
    std::uint32_t maximum_index{};
    std::uint32_t maximum_used{};
    std::uint32_t total_used{};
    std::uint32_t slot_size{};
    std::span<Slot> slots;
    std::byte* base{};
    std::uint32_t allocated_bytes{};
    void initialize(std::uint32_t bytes, std::uint32_t count, CrashAllocator& owner);
    [[nodiscard]] void* allocate(std::uint32_t bytes);
    void release(void* pointer);
    [[nodiscard]] std::uint64_t allocation_size(void* pointer) const;
    void debug_verify() const;
  };
  explicit CrashAllocator(BinnedAllocator* previous);
  void check_allocation_thread() const;
  [[nodiscard]] bool is_owner_thread() const;
  [[nodiscard]] bool in_small_pool(void* pointer) const;
  [[nodiscard]] bool in_large_pool(void* pointer) const;
  [[nodiscard]] Pool* choose_pool(std::uint32_t bytes);
  [[nodiscard]] Pool& find_pool(void* pointer);
  [[nodiscard]] std::byte* take_small_storage(std::uint32_t bytes);
  [[nodiscard]] std::byte* take_bookkeeping_storage(std::uint32_t bytes);
  [[nodiscard]] std::uint64_t recorded_size(void* pointer);
  [[nodiscard]] static std::uint32_t safe_page_size();
  [[nodiscard]] static std::uint32_t small_storage_size();
  [[nodiscard]] static std::uint32_t bookkeeping_storage_size();
  void initialize_pools();

  platform::AllocatorRecursiveMutex activation_mutex_;
  std::atomic<std::uint32_t> crashed_thread_{0};
  std::byte* large_{};
  std::uint32_t large_offset_{};
  std::byte* small_{};
  std::uint32_t small_offset_{};
  std::uint32_t small_bytes_{};
  std::byte* bookkeeping_{};
  std::uint32_t bookkeeping_offset_{};
  std::uint32_t bookkeeping_bytes_{};
  BinnedAllocator* previous_{};
  std::array<Pool, 14> pools_;
};
} // namespace bw::memory
#endif
