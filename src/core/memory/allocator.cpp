#include "core/memory/allocator.hpp"
#include "core/memory/allocator_checks.hpp"
#include "core/memory/binned_allocator.hpp"
#include "core/memory/crash_allocator.hpp"
#include "platform/allocator_platform.hpp"

#include <atomic>
#include <cstdlib>

// Core R.15/C.32/CP.2: paired system object storage, process-lifetime owner and
// atomic crash redirection. Performance MEM.5: caller traffic uses selected bins.
// Reference Unreal f45729c0b6f097038e72b52d366a62baeca3275b.
namespace bw::memory {
namespace {
std::atomic<Allocator*> current_allocator{nullptr};
// UnrealMemory.cpp:347-417 FMemory_GCreateMalloc_ThreadUnsafe: publish base first
// so crash setup can query it, create actual pooled crash reserve, then selected
// Binned3 needs no thread-safety proxy. Optional tracing/stomp instrumentation is
// not selected. NoOnMallocInitialized work is enabled at default tracker0.
Allocator* bootstrap() {
  auto* base = &platform::create_base_allocator();
  current_allocator.store(base, std::memory_order_release);
  (void)CrashAllocator::get(static_cast<BinnedAllocator*>(base));
  BW_ALLOCATOR_CHECK(base->internally_thread_safe());
  return base;
}
} // namespace
// MemoryBase.h:56-93 / UnrealMemory.cpp:658-675 FUseSystemMallocForNew:
// allocator OBJECTS alone use paired system storage to avoid recursive bootstrap.
void* Allocator::operator new(std::size_t bytes) { return std::malloc(bytes); }
void Allocator::operator delete(void* pointer) { std::free(pointer); }
// MemoryBase.h:96-127 FMalloc base has no allocation-owning teardown behavior.
Allocator::Allocator() = default;
Allocator::~Allocator() = default;

// UnrealMemory.cpp:430-439 GCreateMalloc: function-static initialization serializes
// first construction; published current pointer supports later crash replacement.
Allocator& general_allocator() {
  // FMemory.inl:16-52: published allocator is the normal fast path; enter the
  // GCreateMalloc once gate only when no allocator has been published yet.
  if (auto* allocator = current_allocator.load(std::memory_order_acquire)) {
    return *allocator;
  }
  static auto* initialized = bootstrap();
  (void)initialized;
  return *current_allocator.load(std::memory_order_acquire);
}
// GenericPlatformMallocCrash.cpp:308-320 SetAsGMalloc: crash activation publishes
// its preconstructed process-lifetime owner. Atomic publication is the C++ race-
// free extraction of the source global pointer; activation owns its own lock.
void replace_general_allocator_for_crash(Allocator& allocator) {
  current_allocator.store(&allocator, std::memory_order_release);
}
// FMemory.inl:16-52,106-180: ordinary forwarding, null-free avoids bootstrap.
void* allocate(std::size_t bytes, std::uint32_t alignment) {
  return general_allocator().allocate(bytes, alignment);
}
void* reallocate(void* pointer, std::size_t bytes, std::uint32_t alignment) {
  return general_allocator().reallocate(pointer, bytes, alignment);
}
void release(void* pointer) {
  if (pointer != nullptr) {
    general_allocator().release(pointer);
  }
}
std::size_t allocation_size(void* pointer) {
  std::size_t bytes = 0;
  (void)general_allocator().allocation_size(pointer, bytes);
  return bytes;
}
// UnrealMemory.cpp:576-583 SetupTLSCachesOnCurrentThread: initialize the heap
// if necessary, then let its owner create/acquire the calling thread's cache.
void setup_current_thread_cache() { general_allocator().setup_current_thread_cache(); }

// UnrealMemory.cpp:585-591 ClearAndDisableTLSCachesOnCurrentThread: teardown
// only forwards when the heap exists. It must not bootstrap merely to clear.
void clear_current_thread_cache() {
  if (auto* allocator = current_allocator.load(std::memory_order_acquire)) {
    allocator->clear_current_thread_cache();
  }
}

// UnrealMemory.cpp:593-599 MarkTLSCachesAsUsedOnCurrentThread: reacquire the
// existing owner's cache when the heap is present; never initialize it here.
void mark_current_thread_cache_used() {
  if (auto* allocator = current_allocator.load(std::memory_order_acquire)) {
    allocator->mark_current_thread_cache_used();
  }
}

// UnrealMemory.cpp:601-608 MarkTLSCachesAsUnusedOnCurrentThread: relinquish
// existing cache ownership. Actual worker wait sites own calls to this facade.
void mark_current_thread_cache_unused() {
  if (auto* allocator = current_allocator.load(std::memory_order_acquire)) {
    allocator->mark_current_thread_cache_unused();
  }
}
} // namespace bw::memory
