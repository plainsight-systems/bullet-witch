#ifndef BW_PLATFORM_MACOS_PLATFORM_MEMORY_HPP
#define BW_PLATFORM_MACOS_PLATFORM_MEMORY_HPP
#include "bw/platform/memory/platform_memory.hpp"

namespace bw::platform::macos {
// A page-rounded read/write mapping. Its allocator/caller owns explicit release.
// A copied descriptor is an alias, not another owner. No implicit synchronization.
class VirtualMemoryBlock final : public BasicVirtualMemoryBlock {
public:
  VirtualMemoryBlock();
  VirtualMemoryBlock(void* pointer, std::uint32_t size_in_pages);

  // Size rounds up to native pages. Requested alignment must not exceed native
  // page size. Fatal on mapping failure.
  [[nodiscard]] static VirtualMemoryBlock
  allocate_virtual(std::size_t size, std::size_t alignment = virtual_size_alignment());
  // Both are native OS page size on Mac; they are separate contracts for other
  // target backends.
  [[nodiscard]] static std::size_t virtual_size_alignment();
  [[nodiscard]] static std::size_t commit_alignment();
  // Page count multiplied by native size.
  [[nodiscard]] std::size_t actual_size() const;
  // The commit/decommit range precondition: offset and size are multiples of
  // commit_alignment() and the range ends within actual_size().
  [[nodiscard]] bool contains_page_range(std::size_t offset, std::size_t size) const;

  // Page-aligned in-range offset/size on a live mapping.
  // MADV_FREE_REUSE; does not add protection, allocate eagerly or zero bytes.
  void commit(std::size_t offset, std::size_t size);
  // Same preconditions; MADV_FREE_REUSABLE. Contents become discardable. The
  // address range remains mapped; failure is fatal.
  void decommit(std::size_t offset, std::size_t size);
  // Convenience operations use the same range path, converting an interior
  // pointer to an offset or using all pages.
  void commit_by_pointer(void* pointer, std::size_t size);
  void decommit_by_pointer(void* pointer, std::size_t size);
  void commit();
  void decommit();

  // Unmap the complete range, then clear this descriptor.
  // Empty/repeated release is a no-op; other aliases are not cleared. Fatal on
  // unmap failure. No caller may use the region concurrently with release.
  void free_virtual();
};
} // namespace bw::platform::macos
#endif
