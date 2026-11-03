#ifndef BW_CORE_MEMORY_ALLOCATOR_HPP
#define BW_CORE_MEMORY_ALLOCATOR_HPP

#include <cstddef>
#include <cstdint>

// Core R.15: matched allocate/reallocate/free and system-new/delete contracts.
// Performance MEM.5: one facade routes storage into the selected size classes.
namespace bw::memory {

// Deliberately offers no try-allocate recovery guarantee: binned allocation
// failure is fatal.
class Allocator {
public:
  // System-only object storage prevents recursive bootstrap; it does not route
  // caller allocations to the C heap.
  static void* operator new(std::size_t bytes);
  static void operator delete(void* pointer);
  virtual ~Allocator();
  [[nodiscard]] virtual void* allocate(std::size_t bytes, std::uint32_t alignment = 0) = 0;
  [[nodiscard]] virtual void* reallocate(void* pointer, std::size_t bytes,
                                         std::uint32_t alignment = 0) = 0;
  virtual void release(void* pointer) = 0;
  [[nodiscard]] virtual std::size_t quantize(std::size_t bytes,
                                             std::uint32_t alignment = 0) const = 0;
  [[nodiscard]] virtual bool allocation_size(void* pointer, std::size_t& bytes) = 0;
  [[nodiscard]] virtual bool internally_thread_safe() const = 0;
  // Global trim requires a named-thread dispatch broadcast; no local substitute.
  virtual void trim(bool trim_thread_caches) = 0;
  virtual void setup_current_thread_cache() = 0;
  virtual void mark_current_thread_cache_used() = 0;
  virtual void mark_current_thread_cache_unused() = 0;
  virtual void clear_current_thread_cache() = 0;

protected:
  Allocator();
  Allocator(const Allocator&) = delete;
  Allocator& operator=(const Allocator&) = delete;
};

// Process lifetime; bootstrap installs the binned allocator and reserves pooled
// crash storage.
// No injectable fake allocator or resettable singleton is implied.
[[nodiscard]] Allocator& general_allocator();
// The crash owner publishes the reserved allocator after taking its permanent
// crash lock; never resets the normal heap.
void replace_general_allocator_for_crash(Allocator& allocator);
[[nodiscard]] void* allocate(std::size_t bytes, std::uint32_t alignment = 0);
[[nodiscard]] void* reallocate(void* pointer, std::size_t bytes, std::uint32_t alignment = 0);
void release(void* pointer);
[[nodiscard]] std::size_t allocation_size(void* pointer);
// Setup bootstraps the process allocator if absent.
// The other three operations only forward to an already-published allocator;
// clearing/marking an uninitialized thread must not initialize the heap.
// Core R.1 and Performance MEM.6: explicit owner-thread lifecycle via one facade.
void setup_current_thread_cache();
void clear_current_thread_cache();
void mark_current_thread_cache_used();
void mark_current_thread_cache_unused();
} // namespace bw::memory

#endif
