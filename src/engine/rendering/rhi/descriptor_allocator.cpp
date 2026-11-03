#include "bw/rendering/rhi/descriptor_allocator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>

namespace bw::rhi {

// The free list never holds more than capacity / 2 + 1 ranges (every range
// is separated from the next by at least one allocated index), so that much
// is reserved here and free never allocates.
DescriptorIndexAllocator::DescriptorIndexAllocator(std::uint32_t capacity) : capacity_(capacity) {
  free_ranges_.reserve(static_cast<std::size_t>(capacity / 2U) + 1U);
  if (capacity > 0U) {
    free_ranges_.push_back(Range{0U, capacity});
  }
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHICore/Private/RHIDescriptorAllocator.cpp:127-172:
// AllocateInternal takes the first index of the first range that fits and
// shrinks or removes the range. Followed for a count of one, so the first
// range always fits and the lowest free index is returned.
std::optional<std::uint32_t> DescriptorIndexAllocator::allocate() noexcept {
  if (free_ranges_.empty()) {
    return std::nullopt;
  }
  Range& first = free_ranges_.front();
  const std::uint32_t index = first.first;
  ++first.first;
  if (first.first == first.last) {
    free_ranges_.erase(free_ranges_.begin());
  }
  ++allocated_;
  return index;
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHICore/Private/RHIDescriptorAllocator.cpp:174-265:
// FreeInternal finds the index's place in the sorted range list and grows a
// neighbor, merges two neighbors, or inserts a new range. Followed with a
// lower_bound in place of the hand-written binary search. Omitted: the
// outstanding-allocation tracking that detects a double free
// (:189-210, DO_CHECK only); the header states a double free is not detected.
// The insert below stays within the capacity reserved by the constructor, so
// it never allocates and cannot throw.
// NOLINTNEXTLINE(bugprone-exception-escape)
void DescriptorIndexAllocator::free(std::uint32_t index) noexcept {
  // RHIDescriptorAllocator.cpp:179-182: freeing the invalid handle returns
  // at once. Followed: kInvalidDescriptorIndex is a no-op.
  if (index == kInvalidDescriptorIndex) {
    return;
  }
  const auto next =
      std::lower_bound(free_ranges_.begin(), free_ranges_.end(), index,
                       [](const Range& range, std::uint32_t value) { return range.last <= value; });
  const bool joins_next = next != free_ranges_.end() && next->first == index + 1U;
  const bool joins_previous = next != free_ranges_.begin() && std::prev(next)->last == index;
  if (joins_previous && joins_next) {
    std::prev(next)->last = next->last;
    free_ranges_.erase(next);
  } else if (joins_previous) {
    std::prev(next)->last = index + 1U;
  } else if (joins_next) {
    next->first = index;
  } else {
    free_ranges_.insert(next, Range{index, index + 1U});
  }
  --allocated_;
}

DescriptorAllocators::DescriptorAllocators(DescriptorCapacities capacities)
    : pools_{DescriptorIndexAllocator{capacities.read},
             DescriptorIndexAllocator{capacities.sampler},
             DescriptorIndexAllocator{capacities.storage}} {}

std::optional<DescriptorSlot>
DescriptorAllocators::allocate(shaders::DescriptorArray array) noexcept {
  const std::optional<std::uint32_t> index =
      pools_[static_cast<std::size_t>(pool_of(array))].allocate();
  if (!index.has_value()) {
    return std::nullopt;
  }
  return DescriptorSlot{*index, array};
}

void DescriptorAllocators::free(DescriptorSlot slot) noexcept {
  pools_[static_cast<std::size_t>(pool_of(slot.array))].free(slot.index);
}

} // namespace bw::rhi
