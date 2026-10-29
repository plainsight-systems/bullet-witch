#ifndef BW_PLATFORM_MACOS_PLATFORM_MEMORY_HPP
#define BW_PLATFORM_MACOS_PLATFORM_MEMORY_HPP
#include "platform/platform_memory.hpp"

namespace bw::platform::macos {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// Engine/Source/Runtime/Core/Public/Apple/ApplePlatformMemory.h:73-120 and
// Engine/Source/Runtime/Core/Private/Apple/ApplePlatformMemory.cpp:733-796.
// A page-rounded read/write mapping. Its allocator/caller owns explicit release.
// A copied descriptor is an alias, not another owner. No implicit synchronization.
class VirtualMemoryBlock final : public BasicVirtualMemoryBlock {
public:
  VirtualMemoryBlock();
  VirtualMemoryBlock(void* pointer, std::uint32_t size_in_pages);

  // Source AllocateVirtual:745-760. Size rounds up to native pages. Requested
  // alignment must not exceed native page size. Fatal on mapping failure.
  [[nodiscard]] static VirtualMemoryBlock
  allocate_virtual(std::size_t size, std::size_t alignment = virtual_size_alignment());
  // Source GetVirtualSizeAlignment/GetCommitAlignment:733-743. Both are native
  // OS page size on Mac; they are separate contracts for other target backends.
  [[nodiscard]] static std::size_t virtual_size_alignment();
  [[nodiscard]] static std::size_t commit_alignment();
  // Source ApplePlatformMemory.h:112-115. Page count multiplied by native size.
  [[nodiscard]] std::size_t actual_size() const;

  // Source Commit:780-785. Page-aligned in-range offset/size on a live mapping.
  // MADV_FREE_REUSE; does not add protection, allocate eagerly or zero bytes.
  void commit(std::size_t offset, std::size_t size);
  // Source Decommit:787-796. Same preconditions; MADV_FREE_REUSABLE. Contents
  // become discardable. The address range remains mapped; failure is fatal.
  void decommit(std::size_t offset, std::size_t size);
  // Source ApplePlatformMemory.h:92-110. Convenience operations use the same
  // range path, converting an interior pointer to an offset or using all pages.
  void commit_by_pointer(void* pointer, std::size_t size);
  void decommit_by_pointer(void* pointer, std::size_t size);
  void commit();
  void decommit();

  // Source FreeVirtual:764-778. Unmap complete range then clear this descriptor.
  // Empty/repeated release is a no-op; other aliases are not cleared. Fatal on
  // unmap failure. No caller may use the region concurrently with release.
  void free_virtual();
};
} // namespace bw::platform::macos
#endif
