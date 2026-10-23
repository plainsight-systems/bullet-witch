// Native CPU storage capability; selection belongs to target composition.
// Contract: positive byte count and power-of-two alignment >= max_align_t.
// Allocate returns uninitialized aligned storage or null, never throws; release
// receives the exact original tuple once. Providers support concurrent calls
// within and across domains and remain alive through shared domain ownership.
// The domain holds no accounting lock during either call; providers may observe
// its snapshot. Providers own their internal synchronization and must avoid
// cyclic waits or recursive allocation dependencies. No GPU,
// virtual-memory commitment, physical-residency or page-size claim is implied.
// Algorithm: domain reserves -> provider acquires -> domain commits/publishes;
// final owner releases through this same provider. Cost is target-specific.
// Design: cpp-guidelines R.20 (#rr-owner), E.6 (#re-raii) retain provider
// ownership; cpp-performance MEM.1
// (guidelines/memory/MEM.1-arena-allocator-for-frame-scope.md) excludes bulk
// reset for independent lifetimes. Implementation: SL.con.3
// (#rsl-bounds) checks extent in domain; MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) injects backing. See
// memory-unreal-reference.md and memory-bullet-witch-evidence.md under
// research/engine/subsystems/.
#pragma once

#include <cstddef>

namespace bw::memory {
class CpuBacking {
 public:
  CpuBacking() = default;
  CpuBacking(const CpuBacking&) = delete;
  CpuBacking& operator=(const CpuBacking&) = delete;
  CpuBacking(CpuBacking&&) = delete;
  CpuBacking& operator=(CpuBacking&&) = delete;
  virtual ~CpuBacking() = default;
  virtual void* allocate(std::size_t bytes, std::size_t alignment) noexcept = 0;
  virtual void deallocate(void* pointer, std::size_t bytes,
                          std::size_t alignment) noexcept = 0;
};
}  // namespace bw::memory
