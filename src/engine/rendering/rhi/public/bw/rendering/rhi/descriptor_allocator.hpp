#ifndef BW_RENDERING_RHI_DESCRIPTOR_ALLOCATOR_HPP
#define BW_RENDERING_RHI_DESCRIPTOR_ALLOCATOR_HPP
#include "bw/rendering/shaders/root_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Responsibility: the indices into the typed descriptor arrays
// (docs/architecture/rhi.md, "Scope by game need": two descriptor arrays and
// a sampler array; docs/architecture/shaders.md, invariant 5). A view or a
// sampler is given one slot at creation and returns it when destroyed; the
// shader reaches the resource through that 32-bit index. The device owns one
// DescriptorAllocators and serializes calls to it; the types here are pure
// bookkeeping with no API object and no synchronization.
//
// Contract. Three pools, from the root layout: every read array (Texture2D,
// TextureCube, Texture2DArray, Texture3D, Buffer) starts at descriptor
// offset 0 of the resource table, so one index names the same slot in every
// read array and the backend writes the slot of the array that matches the
// resource; the sampler table is its own pool; the two storage arrays share
// the storage pool the same way. pool_of maps an array to its pool.
//   DescriptorIndexAllocator: allocate returns the lowest free index, or no
//   value when none is free (exhaustion is explicit, nothing grows); free
//   returns an index that allocate handed out and that is not currently free
//   (a double free or a stray index is a programming error and the allocator
//   does not detect it in shipping); a freed index is reused by a later
//   allocate, lowest first. allocated() + free_count() == capacity().
//   DescriptorAllocators: one allocator per pool with the given capacities;
//   allocate(array) draws from pool_of(array) and returns a slot carrying the
//   array; free(slot) returns it to that pool; pools are isolated, so
//   exhausting one leaves the others unchanged.
//   Capacity limits: sampler at most shaders::kMaxSamplerDescriptors, storage
//   exactly shaders::kStorageDescriptors (the root signature's range size),
//   read at least 1; the device rejects other values by name at creation
//   (device.hpp, DeviceError::DescriptorCapacityInvalid).
//
// Algorithm: a sorted vector of free ranges [first, last); allocate takes
// the first index of the first range and shrinks or removes it; free
// inserts the index and merges with neighbors. O(ranges) per call; ranges
// stay few because reuse is lowest-first.
//
// Core guidelines read: I.1 (capacities are parameters), E.27 (exhaustion is
// a returned no-value, never a fallback), ES.31 none. Performance guidelines
// read: MEM.2 (a pool of fixed slots with an explicit free list, exhaustion
// fails explicitly, no heap fallback), GDSA.12 (direct-indexed table; the
// index is the handle the GPU reads), CDSA.3 (read for generations: not
// adopted, because a slot's lifetime is the view's lifetime and no index
// escapes the view that owns it; a stale index in a parameter block is a
// caller error the validation layer cannot see, noted in transitions.hpp).
namespace bw::rhi {

inline constexpr std::uint32_t kInvalidDescriptorIndex = 0xFFFFFFFFU;

// The three index spaces of the root layout (root_layout.hpp,
// DescriptorArray and the table each array belongs to).
enum class DescriptorPool : std::uint8_t {
  Read = 0,
  Sampler = 1,
  Storage = 2,
};
inline constexpr std::uint32_t kDescriptorPoolCount = 3;

[[nodiscard]] constexpr DescriptorPool pool_of(shaders::DescriptorArray array) noexcept {
  switch (shaders::placement(array).table) {
  case shaders::RootElement::SamplerTable:
    return DescriptorPool::Sampler;
  case shaders::RootElement::StorageTable:
    return DescriptorPool::Storage;
  case shaders::RootElement::ResourceTable:
  case shaders::RootElement::StaticBlock:
  case shaders::RootElement::DrawBlock:
  case shaders::RootElement::RootConstants:
    return DescriptorPool::Read;
  }
  return DescriptorPool::Read;
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHIDefinitions.h:1397-1430:
// FRHIDescriptorHandle {Index, default MAX_uint32; Type; IsValid()}.
// Followed: the index plus the array it addresses; invalid is the all-ones
// index.
struct DescriptorSlot {
  std::uint32_t index{kInvalidDescriptorIndex};
  shaders::DescriptorArray array{shaders::DescriptorArray::Texture2D};
  [[nodiscard]] constexpr bool valid() const noexcept { return index != kInvalidDescriptorIndex; }
  [[nodiscard]] friend constexpr bool operator==(DescriptorSlot, DescriptorSlot) noexcept = default;
};

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHICore/Public/RHIDescriptorAllocator.h:30-156:
// FRHIDescriptorAllocator keeps free ranges (:79);
// Engine/Source/Runtime/RHICore/Private/RHIDescriptorAllocator.cpp:127-172
// AllocateInternal scans the ranges from the first and :174-265 FreeInternal
// merges the index back; FRHIHeapDescriptorAllocator
// binds one allocator to a heap type. Followed. Omitted:
// FRHIOffsetHeapDescriptorAllocator (sub-ranges of a parent heap), which the
// Direct3D 12 slice decides when it needs per-frame copies; and the
// per-ERHIDescriptorType heaps, replaced by the three pools the root layout
// fixes.
class DescriptorIndexAllocator {
public:
  explicit DescriptorIndexAllocator(std::uint32_t capacity);

  [[nodiscard]] std::optional<std::uint32_t> allocate() noexcept;
  // Precondition: index was returned by allocate and not freed since.
  void free(std::uint32_t index) noexcept;

  [[nodiscard]] std::uint32_t capacity() const noexcept { return capacity_; }
  [[nodiscard]] std::uint32_t allocated() const noexcept { return allocated_; }
  [[nodiscard]] std::uint32_t free_count() const noexcept { return capacity_ - allocated_; }

private:
  struct Range {
    std::uint32_t first{};
    std::uint32_t last{}; // exclusive
  };
  std::vector<Range> free_ranges_{};
  std::uint32_t capacity_{};
  std::uint32_t allocated_{};
};

struct DescriptorCapacities {
  std::uint32_t read{};
  std::uint32_t sampler{};
  std::uint32_t storage{};
};

// True when the capacities meet the limits in the contract.
[[nodiscard]] constexpr bool descriptor_capacities_valid(DescriptorCapacities c) noexcept {
  return c.read >= 1U && c.sampler >= 1U && c.sampler <= shaders::kMaxSamplerDescriptors &&
         c.storage == shaders::kStorageDescriptors;
}

class DescriptorAllocators {
public:
  // Precondition: descriptor_capacities_valid(capacities).
  explicit DescriptorAllocators(DescriptorCapacities capacities);

  [[nodiscard]] std::optional<DescriptorSlot> allocate(shaders::DescriptorArray array) noexcept;
  // Precondition: slot came from allocate and was not freed since.
  void free(DescriptorSlot slot) noexcept;

  [[nodiscard]] const DescriptorIndexAllocator& pool(DescriptorPool pool) const noexcept {
    return pools_[static_cast<std::size_t>(pool)];
  }

private:
  std::array<DescriptorIndexAllocator, kDescriptorPoolCount> pools_;
};

} // namespace bw::rhi
#endif
