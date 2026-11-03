#ifndef BW_RENDERING_RHI_BACKENDS_METAL_METAL_MEMORY_HPP
#define BW_RENDERING_RHI_BACKENDS_METAL_METAL_MEMORY_HPP
// GPU memory of the Metal backend (docs/architecture/rhi.md, invariant 7):
// placement heaps sub-allocated with BlockSuballocator for buffers and
// textures, and the two descriptor heaps the converter's tables point into.
//
// Contract.
//   ResourceMemory keeps one list of MTL::Heap per storage mode (private for
//   GPU-only resources, shared for host-visible ones). allocate places a
//   resource of the size and alignment Metal reports in the first heap of
//   its list with room (BlockSuballocator first fit); when none has room it
//   creates a heap of max(kHeapBlockBytes, size) and places it there; it
//   returns no value when Metal cannot create that heap (OutOfMemory). free
//   returns the range; a heap left with no allocation is retired at once
//   (it takes no allocation and is no longer declared resident) and is
//   released by release_retired once the frame that was being recorded when
//   it emptied has completed, so no command buffer still names it. Every heap is created with
//   tracked hazards. Thread-safe: one mutex, because creation runs on creation threads and
//   destruction and residency on the submission thread (backend.hpp, Context). generation() changes
//   whenever the list changes, so the context knows when to declare
//   residency again.
//   DescriptorHeaps owns two shared buffers of IRDescriptorTableEntry: the
//   resource heap holds the read pool's indices, then the storage pool's;
//   the sampler heap holds the sampler pool's. entry(slot) is the entry a
//   view or sampler writes at creation; the table addresses are what the top
//   level argument buffer carries (binding_structure.hpp).
//
// Core guidelines read: CP.3 and CP.20 (one mutex, scoped locks), R.1 (heaps
// are released by their owner), I.1 (block size and capacities are
// explicit). Performance guidelines read: GPU.9 (large heaps, sub-allocated;
// no API allocation per draw), MEM.5 (pools by storage mode), MEM.2
// (descriptor heaps are fixed arrays of slots).
#include "bw/rendering/rhi/descriptor_allocator.hpp"
#include "bw/rendering/rhi/memory_suballocator.hpp"

#include "metal_api.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace bw::rhi::metal {

enum class StorageClass : std::uint8_t { Private, Shared };

// The heap block size, 64 MiB. The value is a tuning constant, not behavior:
// with two storage modes a larger block commits a large amount of memory
// before the first resource; a resource larger than a block gets a heap of its
// own size.
inline constexpr std::uint64_t kHeapBlockBytes = 64ULL * 1024ULL * 1024ULL;

class HeapBlock;

// Where one resource lives.
struct HeapPlacement {
  HeapBlock* block{};
  Allocation range{};
};

class ResourceMemory {
public:
  // device: borrowed, outlives this object. recording_frame: the backend's
  // frame being recorded, read when a heap empties.
  ResourceMemory(MTL::Device& device, const std::atomic<std::uint64_t>& recording_frame) noexcept;
  ResourceMemory(const ResourceMemory&) = delete;
  ResourceMemory& operator=(const ResourceMemory&) = delete;
  // Releases every heap; every resource placed here was destroyed first.
  ~ResourceMemory();

  [[nodiscard]] std::optional<HeapPlacement> allocate(StorageClass storage, MTL::SizeAndAlign size);
  // Allocates nothing: an emptied heap is marked retired in place.
  void free(const HeapPlacement& placement) noexcept;
  // Releases the retired heaps whose frame is at or below completed.
  void release_retired(std::uint64_t completed_frame);

  [[nodiscard]] std::uint64_t generation() const noexcept;
  // The current heaps, for useHeaps; returns the generation they belong to.
  std::uint64_t heaps(std::vector<MTL::Heap*>& out) const;

private:
  MTL::Device* device_;
  const std::atomic<std::uint64_t>* recording_frame_;
  mutable std::mutex mutex_{};
  std::vector<std::unique_ptr<HeapBlock>> blocks_{};
  std::uint64_t generation_{};
};

// One placement heap and the arithmetic over its bytes.
class HeapBlock {
public:
  HeapBlock(MTL::Heap* heap, StorageClass storage, std::uint64_t capacity) noexcept
      : heap_(heap), allocator_(capacity), storage_(storage) {}
  [[nodiscard]] MTL::Heap* heap() const noexcept { return heap_; }

private:
  friend class ResourceMemory;
  MTL::Heap* heap_;
  BlockSuballocator allocator_;
  StorageClass storage_;
  // Set when the heap emptied: it takes no allocation and is released once
  // retired_frame has completed.
  bool retired_{};
  std::uint64_t retired_frame_{};
};

// The resource options of a heap-placed resource of this storage class.
[[nodiscard]] MTL::ResourceOptions resource_options(StorageClass storage) noexcept;

// The resources unordered access views write through the storage table.
// A view adds its resource at creation and removes it at destruction (one
// entry per view); the context declares them Read|Write on an encoder whose
// pipeline uses the storage table (metal_context.cpp, residency).
// generation() changes whenever the set changes. Thread-safe: views are
// created on creation threads and destroyed and declared on the submission
// thread.
class WritableResources {
public:
  void add(MTL::Resource& resource);
  // Allocates nothing.
  void remove(MTL::Resource& resource) noexcept;
  [[nodiscard]] std::uint64_t generation() const noexcept;
  std::uint64_t snapshot(std::vector<const MTL::Resource*>& out) const;

private:
  mutable std::mutex mutex_{};
  std::vector<MTL::Resource*> resources_{};
  std::uint64_t generation_{};
};

// The two shared buffers of a DescriptorHeaps, both owned by it once given.
struct DescriptorHeapBuffers {
  MTL::Buffer* resources{};
  MTL::Buffer* samplers{};
};

class DescriptorHeaps {
public:
  DescriptorHeaps(DescriptorHeapBuffers buffers, DescriptorCapacities capacities) noexcept
      : resources_(buffers.resources), samplers_(buffers.samplers), capacities_(capacities) {}
  DescriptorHeaps(const DescriptorHeaps&) = delete;
  DescriptorHeaps& operator=(const DescriptorHeaps&) = delete;
  ~DescriptorHeaps();

  // Precondition: slot came from the device's allocators of these capacities.
  [[nodiscard]] IRDescriptorTableEntry& entry(DescriptorSlot slot) const noexcept;
  [[nodiscard]] std::uint64_t resource_table_address() const noexcept;
  [[nodiscard]] std::uint64_t storage_table_address() const noexcept;
  [[nodiscard]] std::uint64_t sampler_table_address() const noexcept;
  [[nodiscard]] MTL::Buffer* resource_heap() const noexcept { return resources_; }
  [[nodiscard]] MTL::Buffer* sampler_heap() const noexcept { return samplers_; }

private:
  MTL::Buffer* resources_;
  MTL::Buffer* samplers_;
  DescriptorCapacities capacities_;
};

} // namespace bw::rhi::metal
#endif
