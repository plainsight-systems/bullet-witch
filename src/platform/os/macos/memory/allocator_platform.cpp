#include "bw/platform/memory/allocator_platform.hpp"
#include "allocator_checks.hpp"
#include "binned/binned_allocator.hpp"

#include <atomic>
#include <bit>
#include <cerrno>
#include <dispatch/dispatch.h>
#include <mach/vm_page_size.h>
#include <mach/vm_statistics.h>
#include <os/log.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/sysctl.h>

// Core CP.2: immutable constants and atomic pressure publication. Performance
// MEM.5: platform selects Binned3, never substitutes libc for client allocations.
// Reference Unreal f45729c0b6f097038e72b52d366a62baeca3275b.
namespace bw::platform {
namespace {
std::atomic<AllocatorMemoryPressure> pressure{AllocatorMemoryPressure::nominal};
// MacPlatformMemory.cpp:68-80: callbacks run on native main dispatch queue.
void normal_pressure(void*) { pressure.store(AllocatorMemoryPressure::nominal); }
void warning_pressure(void*) { pressure.store(AllocatorMemoryPressure::warning); }
void critical_pressure(void*) { pressure.store(AllocatorMemoryPressure::critical); }
// Extraction uses dispatch's C function handler instead of Objective-C blocks;
// same native source, main queue and process-lifetime observer ownership.
void observe_pressure(unsigned long mask, dispatch_function_t callback) {
  auto source = dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0U, mask,
                                       dispatch_get_main_queue());
  dispatch_source_set_event_handler_f(source, callback);
  dispatch_activate(source);
  dispatch_retain(source);
}
} // namespace
// MacPlatformMemory.cpp:228-258 GetConstants: native physical memory and page
// size; rounded physical size is a hash-sizing hint, never a virtual-address cap.
const AllocatorPlatformConstants& allocator_constants() {
  static const auto constants = [] {
    AllocatorPlatformConstants value;
    int selectors[] = {CTL_HW, HW_MEMSIZE};
    std::size_t bytes = sizeof(value.total_physical);
    const int result = sysctl(selectors, 2U, &value.total_physical, &bytes, nullptr, 0U);
    BW_ALLOCATOR_REQUIRE(result == 0 && value.total_physical != 0U);
    value.native_page_size = static_cast<std::uint32_t>(vm_page_size);
    value.address_limit = std::bit_ceil(value.total_physical);
    return value;
  }();
  return constants;
}
// BaseAllocator:68-175 selected game Binned3 branch; observers precede creation.
// Facade's one-time bootstrap serializes this entry; no alternate allocator flags.
memory::Allocator& create_base_allocator() {
  observe_pressure(DISPATCH_MEMORYPRESSURE_NORMAL, normal_pressure);
  observe_pressure(DISPATCH_MEMORYPRESSURE_WARN, warning_pressure);
  observe_pressure(DISPATCH_MEMORYPRESSURE_CRITICAL, critical_pressure);
  static auto* instance = new memory::BinnedAllocator;
  return *instance;
}
// MacPlatformMemory pressure status is queried independently of allocation.
AllocatorMemoryPressure allocator_memory_pressure() { return pressure.load(); }
// ApplePlatformTLS GetCurrentThreadId: native Mach identity, not a hashed C++ id.
std::uint32_t current_allocator_thread_id() { return pthread_mach_thread_np(pthread_self()); }
// ApplePlatformMemory.cpp:639-648 BinnedAllocFromOS selected non-Binned2 branch:
// crash reserves use tag240 ordinary RW anonymous mapping; return null on failure.
void* allocate_crash_pool_from_os(std::size_t bytes) {
  // Darwin uses anonymous mmap's fd slot for native VM tags.
  // NOLINTNEXTLINE(clang-analyzer-unix.StdCLibraryFunctions)
  void* pointer = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON,
                       VM_MAKE_TAG(VM_MEMORY_APPLICATION_SPECIFIC_1), 0);
  if (pointer == MAP_FAILED) {
    os_log_error(OS_LOG_DEFAULT, "Crash reserve mmap failed: bytes=%{public}zu errno=%{public}d",
                 bytes, errno);
    return nullptr;
  }
  return pointer;
}
} // namespace bw::platform
