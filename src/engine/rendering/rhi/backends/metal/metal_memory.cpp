#include "metal_memory.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace bw::rhi::metal {

namespace {

[[nodiscard]] MTL::StorageMode storage_mode(StorageClass storage) noexcept {
  return storage == StorageClass::Shared ? MTL::StorageModeShared : MTL::StorageModePrivate;
}

} // namespace

// A heap-backed resource is created with tracked hazards.
MTL::ResourceOptions resource_options(StorageClass storage) noexcept {
  const NS::UInteger mode = storage == StorageClass::Shared ? MTL::ResourceStorageModeShared
                                                            : MTL::ResourceStorageModePrivate;
  return static_cast<MTL::ResourceOptions>(mode | MTL::ResourceCPUCacheModeDefaultCache |
                                           MTL::ResourceHazardTrackingModeTracked);
}

ResourceMemory::ResourceMemory(MTL::Device& device,
                               const std::atomic<std::uint64_t>& recording_frame) noexcept
    : device_(&device), recording_frame_(&recording_frame) {}

ResourceMemory::~ResourceMemory() {
  for (const std::unique_ptr<HeapBlock>& block : blocks_) {
    block->heap_->release();
  }
}

// Takes the first free block of the storage class that fits the aligned size,
// else creates a placement heap of max(kHeapBlockBytes, size + alignment) with
// the storage class's storage mode, the default CPU cache mode and tracked
// hazards, and retries. The block arithmetic is in BlockSuballocator. A null
// heap is OutOfMemory.
std::optional<HeapPlacement> ResourceMemory::allocate(StorageClass storage,
                                                      MTL::SizeAndAlign size) {
  const std::uint64_t bytes = size.size == 0U ? 1U : size.size;
  const std::uint64_t alignment = size.align == 0U ? 1U : size.align;
  const std::scoped_lock lock(mutex_);
  for (const std::unique_ptr<HeapBlock>& block : blocks_) {
    if (block->storage_ != storage || block->retired_) {
      continue;
    }
    const std::optional<Allocation> range = block->allocator_.allocate(bytes, alignment);
    if (range.has_value()) {
      return HeapPlacement{block.get(), *range};
    }
  }
  const AutoreleaseScope pool;
  const ScopedRelease<MTL::HeapDescriptor> descriptor{MTL::HeapDescriptor::alloc()->init()};
  descriptor->setSize(std::max(kHeapBlockBytes, bytes + alignment));
  descriptor->setStorageMode(storage_mode(storage));
  descriptor->setCpuCacheMode(MTL::CPUCacheModeDefaultCache);
  descriptor->setType(MTL::HeapTypePlacement);
  descriptor->setHazardTrackingMode(MTL::HazardTrackingModeTracked);
  MTL::Heap* heap = device_->newHeap(descriptor.get());
  if (heap == nullptr) {
    return std::nullopt;
  }
  blocks_.push_back(std::make_unique<HeapBlock>(heap, storage, heap->size()));
  ++generation_;
  HeapBlock& block = *blocks_.back();
  const std::optional<Allocation> range = block.allocator_.allocate(bytes, alignment);
  if (!range.has_value()) {
    return std::nullopt;
  }
  return HeapPlacement{&block, *range};
}

// Returns the range to its block; when the block is then empty, the heap is
// retired now (no allocation, not resident) and released by release_retired
// once the frame being recorded has completed, so it is released after the
// GPU is done.
void ResourceMemory::free(const HeapPlacement& placement) noexcept {
  if (placement.block == nullptr) {
    return;
  }
  const std::scoped_lock lock(mutex_);
  HeapBlock* block = placement.block;
  block->allocator_.free(placement.range.offset);
  if (!block->allocator_.empty()) {
    return;
  }
  block->retired_ = true;
  block->retired_frame_ = recording_frame_->load(std::memory_order_acquire);
  ++generation_;
}

void ResourceMemory::release_retired(std::uint64_t completed_frame) {
  const std::scoped_lock lock(mutex_);
  const auto released = std::stable_partition(
      blocks_.begin(), blocks_.end(), [completed_frame](const std::unique_ptr<HeapBlock>& block) {
        return !block->retired_ || block->retired_frame_ > completed_frame;
      });
  for (auto it = released; it != blocks_.end(); ++it) {
    (*it)->heap_->release();
  }
  blocks_.erase(released, blocks_.end());
}

std::uint64_t ResourceMemory::generation() const noexcept {
  const std::scoped_lock lock(mutex_);
  return generation_;
}

std::uint64_t ResourceMemory::heaps(std::vector<MTL::Heap*>& out) const {
  const std::scoped_lock lock(mutex_);
  out.clear();
  out.reserve(blocks_.size());
  for (const std::unique_ptr<HeapBlock>& block : blocks_) {
    if (!block->retired_) {
      out.push_back(block->heap_);
    }
  }
  return generation_;
}

void WritableResources::add(MTL::Resource& resource) {
  const std::scoped_lock lock(mutex_);
  resources_.push_back(&resource);
  ++generation_;
}

void WritableResources::remove(MTL::Resource& resource) noexcept {
  const std::scoped_lock lock(mutex_);
  const auto found = std::find(resources_.begin(), resources_.end(), &resource);
  if (found != resources_.end()) {
    resources_.erase(found);
    ++generation_;
  }
}

std::uint64_t WritableResources::generation() const noexcept {
  const std::scoped_lock lock(mutex_);
  return generation_;
}

std::uint64_t WritableResources::snapshot(std::vector<const MTL::Resource*>& out) const {
  const std::scoped_lock lock(mutex_);
  out.assign(resources_.begin(), resources_.end());
  return generation_;
}

DescriptorHeaps::~DescriptorHeaps() {
  resources_->release();
  samplers_->release();
}

IRDescriptorTableEntry& DescriptorHeaps::entry(DescriptorSlot slot) const noexcept {
  auto* sampler_entries = static_cast<IRDescriptorTableEntry*>(samplers_->contents());
  auto* resource_entries = static_cast<IRDescriptorTableEntry*>(resources_->contents());
  switch (pool_of(slot.array)) {
  case DescriptorPool::Sampler:
    return sampler_entries[slot.index];
  case DescriptorPool::Storage:
    return resource_entries[static_cast<std::size_t>(capacities_.read) + slot.index];
  case DescriptorPool::Read:
    break;
  }
  return resource_entries[slot.index];
}

std::uint64_t DescriptorHeaps::resource_table_address() const noexcept {
  return resources_->gpuAddress();
}

std::uint64_t DescriptorHeaps::storage_table_address() const noexcept {
  return resources_->gpuAddress() +
         static_cast<std::uint64_t>(capacities_.read) * sizeof(IRDescriptorTableEntry);
}

std::uint64_t DescriptorHeaps::sampler_table_address() const noexcept {
  return samplers_->gpuAddress();
}

} // namespace bw::rhi::metal
