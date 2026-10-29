#ifndef BW_CORE_TASKS_TASK_STORAGE_HPP
#define BW_CORE_TASKS_TASK_STORAGE_HPP

#include <cstddef>
#include <cstdint>

// CP.2: only the producer mutates its bump pointer; consumers release through
// the shared count. CONC.2: final count acquire observes the closed block size.
namespace bw::tasks {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// TaskGraphInterfaces.h:451-465 and ConcurrentLinearAllocator.h:187-548.
// 64KiB concurrent linear task blocks, per-allocation headers on selected Mac,
// producer-local bump/count, cross-thread release and one-block TLS cache.
// Returned storage is raw; callers construct/destroy task objects themselves.
[[nodiscard]] void* allocate_task_storage(std::size_t bytes, std::uint32_t alignment);
void release_task_storage(void* pointer);
} // namespace bw::tasks
#endif
