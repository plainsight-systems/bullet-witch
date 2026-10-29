#ifndef BW_PLATFORM_PLATFORM_MEMORY_HPP
#define BW_PLATFORM_PLATFORM_MEMORY_HPP
#include <cstddef>
#include <cstdint>

// Core R.1/I.11: the allocator owns the mapping; this is its non-owning descriptor.
// Performance MEM.6: native mapping sits behind one platform contract. The rule
// does not replace Unreal's allocator bootstrap with dependency injection.
namespace bw::platform {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMemory.h:469-500.
// Descriptor copies alias one mapping. Exactly one caller/allocator releases it;
// aliases become invalid at release. No destructor implicitly releases memory.
class BasicVirtualMemoryBlock {
public:
  BasicVirtualMemoryBlock();
  BasicVirtualMemoryBlock(void* pointer, std::uint32_t size_in_pages);
  BasicVirtualMemoryBlock(const BasicVirtualMemoryBlock&) = default;
  BasicVirtualMemoryBlock& operator=(const BasicVirtualMemoryBlock&) = default;
  [[nodiscard]] std::uint32_t actual_size_in_pages() const;
  [[nodiscard]] void* virtual_pointer() const;

protected:
  void* pointer_{};
  std::uint32_t size_in_pages_{};
};
} // namespace bw::platform
#endif
