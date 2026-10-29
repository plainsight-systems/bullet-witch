#include "platform/platform_memory.hpp"

// Core R.1/I.11: descriptor aliases do not acquire another release obligation.
// MEM.6: this common type contains no native mapping policy.
// References: Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// Engine/Source/Runtime/Core/Public/GenericPlatform/GenericPlatformMemory.h.
namespace bw::platform {
// FBasicVirtualMemoryBlock:477-481 initializes an empty descriptor.
BasicVirtualMemoryBlock::BasicVirtualMemoryBlock() = default;

// FBasicVirtualMemoryBlock:483-487 records existing mapping metadata, not bytes.
BasicVirtualMemoryBlock::BasicVirtualMemoryBlock(void* pointer, std::uint32_t size_in_pages)
    : pointer_(pointer), size_in_pages_(size_in_pages) {}

// GetActualSizeInPages:492-495 reads the recorded count without modifying memory.
std::uint32_t BasicVirtualMemoryBlock::actual_size_in_pages() const { return size_in_pages_; }

// GetVirtualPointer:497-500 returns the address; lifetime stays with its owner.
void* BasicVirtualMemoryBlock::virtual_pointer() const { return pointer_; }
} // namespace bw::platform
