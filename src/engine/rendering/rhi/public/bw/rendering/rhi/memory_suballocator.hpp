#ifndef BW_RENDERING_RHI_MEMORY_SUBALLOCATOR_HPP
#define BW_RENDERING_RHI_MEMORY_SUBALLOCATOR_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Responsibility: the offset arithmetic behind every backend pool
// (docs/architecture/rhi.md, invariant 7: GPU memory comes from backend-owned
// pools and sub-allocators; there is no API allocation per draw). A backend
// owns one API heap or buffer per pool and one BlockSuballocator over its
// byte range; this type never sees the API object.
//
// Contract: allocate(size, alignment) returns the first free block, in
// address order, into which an allocation of `size` bytes at an offset that
// is a multiple of `alignment` fits, or no value when none does (exhaustion
// is explicit; nothing grows). The returned Allocation covers
// [offset, offset + size) with offset aligned; the padding between the free
// block's start and offset belongs to the allocation and is returned with
// it (departure from the pin's SplitBlock, MetalBuffer.cpp:1520-1536, which
// cuts the padding off as its own free block: one block per allocation
// keeps free a single lookup). free(offset) takes the offset of a live allocation and merges the
// block with free neighbors, so after every allocation is freed the
// allocator holds one block of `capacity` bytes again. Preconditions: size
// >= 1, alignment a power of two, capacity >= 1; free of an offset that is
// not a live allocation's offset is a programming error the allocator does
// not detect in shipping. Single-threaded: the owning backend serializes.
//
// Algorithm: a sorted vector of blocks, each free or used, covering the
// range exactly; allocate scans for the first free block that fits, splits
// it into [used][free remainder]; free marks the block free and merges with
// an adjacent free block on either side. O(blocks) per call.
//
// Core guidelines read: I.1 (capacity and alignment are parameters), E.27
// (exhaustion is a returned no-value, never a fallback to a new API
// allocation, which is what would hide an under-sized pool). Performance
// guidelines read: GPU.9 (large heaps, sub-allocation, no per-resource API
// allocation in the frame loop), MEM.5 (the backend keeps separate pools per
// lifetime and storage mode; this type serves each), MEM.2 (explicit
// exhaustion, no silent heap fallback), EMB.4 was read: a first-fit list is
// chosen over TLSF because pool counts are small and the behavior is what
// the pinned Metal heap does.
namespace bw::rhi {

struct Allocation {
  std::uint64_t offset{};
  std::uint64_t size{};
  [[nodiscard]] friend constexpr bool operator==(Allocation, Allocation) noexcept = default;
};

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalBuffer.cpp:1684-1764:
// FMetalResourceHeap::FindOrAllocateBlock walks the free list for the first
// block that fits the aligned size; :1514-1561 SplitBlock cuts the remainder
// off as a new free block; :1563-1577 MergeBlocks and :1586-1682 FreeBlock
// join a freed block with its free neighbors. Followed as pure arithmetic.
// Omitted: the per-ResourceOptions block lists and the heap growth of
// :1729-1763 by METALHEAP_DEFAULT_BLOCKSIZE, 512 MiB on Mac (:1579-1584)
// (one allocator per backend pool, pools sized by the backend at device
// creation), and the allocation handle table of
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalBuffer.h:534-537 (the
// Allocation value is the handle).
class BlockSuballocator {
public:
  explicit BlockSuballocator(std::uint64_t capacity);

  [[nodiscard]] std::optional<Allocation> allocate(std::uint64_t size,
                                                   std::uint64_t alignment) noexcept;
  // Precondition: offset is the offset of a live allocation.
  void free(std::uint64_t offset) noexcept;

  [[nodiscard]] std::uint64_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::uint64_t free_bytes() const noexcept { return free_bytes_; }
  // The largest allocation that would succeed with alignment 1; 0 when full.
  [[nodiscard]] std::uint64_t largest_free_block() const noexcept;
  [[nodiscard]] bool empty() const noexcept { return free_bytes_ == capacity_; }
  [[nodiscard]] std::size_t block_count() const noexcept { return blocks_.size(); }

private:
  struct Block {
    std::uint64_t start{};
    std::uint64_t end{};               // exclusive
    std::uint64_t allocation_offset{}; // aligned offset when used
    bool used{};
  };
  std::vector<Block> blocks_{};
  std::uint64_t capacity_{};
  std::uint64_t free_bytes_{};
};

} // namespace bw::rhi
#endif
