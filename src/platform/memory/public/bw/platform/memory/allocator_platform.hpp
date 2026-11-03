#ifndef BW_PLATFORM_ALLOCATOR_PLATFORM_HPP
#define BW_PLATFORM_ALLOCATOR_PLATFORM_HPP

#include <cstddef>
#include <cstdint>

#include "bw/platform/os/macos/platform_memory.hpp"

namespace bw::memory {
class Allocator;
}
namespace bw::platform {
// Selected target alias only; core allocator algorithms use this shared name.
using AllocatorVirtualMemoryBlock = macos::VirtualMemoryBlock;

// address_limit is the rounded physical-memory value used as a hash-sizing
// hint; it is not an OS address validity limit.
struct AllocatorPlatformConstants {
  std::uint64_t total_physical{};
  std::uint64_t address_start{};
  std::uint64_t address_limit{};
  std::uint32_t native_page_size{};
};
[[nodiscard]] const AllocatorPlatformConstants& allocator_constants();

// Establish native memory-pressure observers, construct the binned allocator
// once using its system-allocation object storage.
// Facade still has separate required crash-allocator bootstrap after this call.
[[nodiscard]] memory::Allocator& create_base_allocator();
// Crash reserves map directly with native tag 240; failure returns null for
// crash-bootstrap diagnostics.
[[nodiscard]] void* allocate_crash_pool_from_os(std::size_t bytes);
// Uses the Mach thread identity.
[[nodiscard]] std::uint32_t current_allocator_thread_id();
// The memory-pressure observers publish the latest native status.
enum class AllocatorMemoryPressure : std::uint8_t { nominal, warning, critical };
[[nodiscard]] AllocatorMemoryPressure allocator_memory_pressure();
} // namespace bw::platform

#endif
