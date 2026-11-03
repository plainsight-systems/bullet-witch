#include "bw/platform/memory/platform_memory.hpp"

// Core R.1/I.11: descriptor aliases do not acquire another release obligation.
// MEM.6: this common type contains no native mapping policy.
namespace bw::platform {
// Initializes an empty descriptor.
BasicVirtualMemoryBlock::BasicVirtualMemoryBlock() = default;

// Records existing mapping metadata, not bytes.
BasicVirtualMemoryBlock::BasicVirtualMemoryBlock(void* pointer, std::uint32_t size_in_pages)
    : pointer_(pointer), size_in_pages_(size_in_pages) {}

// Reads the recorded count without modifying memory.
std::uint32_t BasicVirtualMemoryBlock::actual_size_in_pages() const { return size_in_pages_; }

// Returns the address; lifetime stays with its owner.
void* BasicVirtualMemoryBlock::virtual_pointer() const { return pointer_; }
} // namespace bw::platform
