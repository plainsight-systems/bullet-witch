#include "core/memory/binned_bit_tree.hpp"

#include "core/memory/allocator_checks.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <limits>

// Core C.32/CP.2: caller owns and synchronizes backing. Performance MEM.2:
// availability summaries stay outside client slots. References below are Unreal
// f45729c0b6f097038e72b52d366a62baeca3275b, HAL/MallocBinnedCommon.{h,cpp}.
namespace bw::memory {
namespace {
constexpr auto full = std::numeric_limits<std::uint64_t>::max();
constexpr auto unavailable = std::numeric_limits<std::uint32_t>::max();
} // namespace
// .h:128-132 FBitTree: begin unbound, without implicit allocation.
BinnedBitTree::BinnedBitTree() = default;

// .h:134-153 GetMemoryRequirements: geometric summary rows minus unused leaves.
std::uint32_t BinnedBitTree::memory_requirement(std::uint32_t capacity) {
  std::uint32_t coverage = 64U;
  std::uint32_t words = 1U;
  std::uint32_t row_words = 1U;
  while (coverage < capacity) {
    coverage *= 64U;
    row_words *= 64U;
    words += row_words;
  }
  return (words - (coverage - capacity) / 64U) * 8U;
}

// .cpp:168-227 FBitTreeInit: describe rows, bind caller memory and exclude padding.
void BinnedBitTree::initialize(std::uint32_t capacity, void* backing,
                               [[maybe_unused]] std::uint32_t bytes, bool occupied) {
  desired_capacity_ = capacity;
  capacity_ = 64U;
  rows_ = 1U;
  last_row_offset_ = 0U;
  std::uint32_t words = 1U;
  std::uint32_t row_words = 1U;
  std::array<std::uint32_t, 10> offsets{};
  std::array<std::uint32_t, 10> counts{};
  counts[0] = 1U;
  while (capacity_ < capacity) {
    capacity_ *= 64U;
    row_words *= 64U;
    last_row_offset_ = words;
    BW_ALLOCATOR_CHECK(rows_ < offsets.size());
    offsets[rows_] = words;
    counts[rows_++] = row_words;
    words += row_words;
  }
  allocation_bytes_ = memory_requirement(capacity);
  BW_ALLOCATOR_CHECK(backing && allocation_bytes_ <= bytes);
  bits_ = static_cast<std::uint64_t*>(backing);
  std::memset(bits_, occupied ? 0xff : 0, allocation_bytes_);
  if (occupied) {
    return;
  }
  // FBitTreeInit: full nonexistent child subtrees must be occupied at every row;
  // otherwise first-free descent could leave the truncated backing allocation.
  std::uint32_t items_per_bit = 64U;
  for (std::uint32_t row = rows_ - 1U; row > 0U;) {
    --row;
    const auto excluded = counts[row] * 64U - (capacity + items_per_bit - 1U) / items_per_bit;
    const auto whole_words = excluded / 64U;
    const auto tail_bits = excluded % 64U;
    for (auto word = counts[row] - whole_words; word < counts[row]; ++word) {
      bits_[offsets[row] + word] = full;
    }
    if (tail_bits != 0U) {
      bits_[offsets[row] + counts[row] - whole_words - 1U] = full << (64U - tail_bits);
    }
    items_per_bit *= 64U;
  }
  if (capacity % 64U != 0U) {
    bits_[allocation_bytes_ / 8U - 1U] = full << (capacity % 64U);
  }
}

// .cpp:229-276 AllocBit: first-free descent, then identical indexed propagation.
// Extraction shares the source indexed marking algorithm instead of duplicating it.
std::uint32_t BinnedBitTree::allocate() {
  const auto index = next_available();
  if (index != unavailable) {
    allocate(index);
  }
  return index;
}

// .cpp:289-317 AllocBit(Index): set leaf and carry newly full summaries upward.
void BinnedBitTree::allocate(std::uint32_t index) {
  BW_ALLOCATOR_CHECK(index < desired_capacity_ && !is_allocated(index));
  auto word = last_row_offset_ + index / 64U;
  bits_[word] |= std::uint64_t{1} << (index % 64U);
  while (word != 0U && bits_[word] == full) {
    const auto child = (word - 1U) % 64U;
    word = (word - 1U) / 64U;
    bits_[word] |= std::uint64_t{1} << child;
  }
}

// .cpp:278-287 IsAllocated: read the checked leaf bit.
bool BinnedBitTree::is_allocated(std::uint32_t index) const {
  BW_ALLOCATOR_CHECK(index < desired_capacity_);
  return (bits_[last_row_offset_ + index / 64U] & (std::uint64_t{1} << (index % 64U))) != 0U;
}

// .cpp:319-345 NextAllocBit: descend the first clear child without mutating rows.
std::uint32_t BinnedBitTree::next_available() const {
  if (bits_[0] == full) {
    return unavailable;
  }
  std::uint32_t word = 0U;
  std::uint32_t result = 0U;
  for (std::uint32_t row = 0U; row < rows_; ++row) {
    const auto child = static_cast<std::uint32_t>(std::countr_zero(~bits_[word]));
    result = result * 64U + child;
    word = word * 64U + 1U + child;
  }
  return result;
}

// .cpp:347-413 NextAllocBit(Start): mask earlier leaves, ascend to a later branch,
// then descend that branch's first clear child. Never wrap around to earlier bins.
std::uint32_t BinnedBitTree::next_available(std::uint32_t start) const {
  if (bits_[0] == full) {
    return unavailable;
  }
  BW_ALLOCATOR_CHECK(start < desired_capacity_);
  auto word = last_row_offset_ + start / 64U;
  auto row = rows_ - 1U;
  auto bit = start % 64U;
  if ((bits_[word] & (std::uint64_t{1} << bit)) == 0U) {
    return start;
  }
  auto candidate = bits_[word] | (full >> (63U - bit));
  while (candidate == full) {
    if (row == 0U) {
      return unavailable;
    }
    --row;
    bit = (word - 1U) % 64U;
    word = (word - 1U) / 64U;
    candidate = bits_[word] | (full >> (63U - bit));
  }
  for (;;) {
    bit = static_cast<std::uint32_t>(std::countr_zero(~candidate));
    if (row == rows_ - 1U) {
      return (word - last_row_offset_) * 64U + bit;
    }
    word = word * 64U + 1U + bit;
    candidate = bits_[word];
    ++row;
  }
}

// .cpp:415-443 FreeBit: clear leaf, then only formerly full ancestor summaries.
void BinnedBitTree::release(std::uint32_t index) {
  BW_ALLOCATOR_CHECK(index < desired_capacity_ && is_allocated(index));
  auto word = last_row_offset_ + index / 64U;
  auto bit = index % 64U;
  for (;;) {
    const bool was_full = bits_[word] == full;
    bits_[word] &= ~(std::uint64_t{1} << bit);
    if (!was_full || word == 0U) {
      return;
    }
    bit = (word - 1U) % 64U;
    word = (word - 1U) / 64U;
  }
}

// .cpp:445-459 CountOnes: whole leaf words then masked partial end word.
std::uint32_t BinnedBitTree::count_occupied(std::uint32_t end) const {
  BW_ALLOCATOR_CHECK(end <= desired_capacity_);
  std::uint32_t count = 0U;
  auto word = last_row_offset_;
  while (end >= 64U) {
    count += static_cast<std::uint32_t>(std::popcount(bits_[word++]));
    end -= 64U;
  }
  if (end != 0U) {
    count += static_cast<std::uint32_t>(std::popcount(bits_[word] << (64U - end)));
  }
  return count;
}
} // namespace bw::memory
