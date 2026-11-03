#ifndef BW_PLATFORM_ALLOCATOR_SYNC_HPP
#define BW_PLATFORM_ALLOCATOR_SYNC_HPP

#include "bw/platform/os/macos/allocator_sync.hpp"

// Target selection. Native definitions stay in the Mac target.
namespace bw::platform {
using macos::allocator_spin_yield;
using AllocatorRecursiveMutex = macos::AllocatorRecursiveMutex;
using AllocatorManualResetEvent = macos::AllocatorManualResetEvent;
inline constexpr auto allocator_cache_line_size = macos::allocator_cache_line_size;
} // namespace bw::platform

#endif
