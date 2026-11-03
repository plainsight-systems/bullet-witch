#include "bw/rendering/rhi/memory_suballocator.hpp"

#include "bw/memory/alignment.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace bw::rhi {

BlockSuballocator::BlockSuballocator(std::uint64_t capacity)
    : capacity_(capacity), free_bytes_(capacity) {
  if (capacity > 0U) {
    blocks_.push_back(Block{0U, capacity, 0U, false});
  }
}

// Walks the blocks in address order for the first free one that holds the
// aligned size and cuts the remainder off as a new free block. The
// alignment padding stays inside the used block and returns with it on free
// (memory_suballocator.hpp). The signature is the header's; the split's
// insert may allocate, and an allocation failure inside this noexcept
// function terminates.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters,bugprone-exception-escape)
std::optional<Allocation> BlockSuballocator::allocate(std::uint64_t size,
                                                      std::uint64_t alignment) noexcept {
  for (std::size_t i = 0; i < blocks_.size(); ++i) {
    const Block block = blocks_[i];
    if (block.used) {
      continue;
    }
    const std::uint64_t offset = bw::memory::align_up(block.start, alignment);
    if (offset < block.start || offset > block.end || block.end - offset < size) {
      continue;
    }
    const std::uint64_t used_end = offset + size;
    blocks_[i] = Block{block.start, used_end, offset, true};
    if (used_end < block.end) {
      blocks_.insert(blocks_.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                     Block{used_end, block.end, 0U, false});
    }
    free_bytes_ -= used_end - block.start;
    return Allocation{offset, size};
  }
  return std::nullopt;
}

// Marks the block free and joins it with a free neighbor on either side.
void BlockSuballocator::free(std::uint64_t offset) noexcept {
  for (std::size_t i = 0; i < blocks_.size(); ++i) {
    Block& block = blocks_[i];
    if (!block.used || block.allocation_offset != offset) {
      continue;
    }
    block.used = false;
    block.allocation_offset = 0U;
    free_bytes_ += block.end - block.start;
    if (i + 1U < blocks_.size() && !blocks_[i + 1U].used) {
      block.end = blocks_[i + 1U].end;
      blocks_.erase(blocks_.begin() + static_cast<std::ptrdiff_t>(i) + 1);
    }
    if (i > 0U && !blocks_[i - 1U].used) {
      blocks_[i - 1U].end = blocks_[i].end;
      blocks_.erase(blocks_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return;
  }
}

std::uint64_t BlockSuballocator::largest_free_block() const noexcept {
  std::uint64_t largest = 0;
  for (const Block& block : blocks_) {
    if (!block.used && block.end - block.start > largest) {
      largest = block.end - block.start;
    }
  }
  return largest;
}

} // namespace bw::rhi
