#include "binned/binned_size_classes.hpp"

#include "allocator_checks.hpp"
#include "bw/memory/alignment.hpp"
#include <algorithm>
#include <initializer_list>

// Core C.32: these tables own values, never client storage. Performance MEM.5:
// keep size segregation; this class table, not illustrative rules, sets bins.
namespace bw::memory {
// The align helper rounds to the selected power-of-two boundary; alignment zero
// is handled by the callers first.

// Build native-page-compatible block multiples, sort the listed classes, then
// append 4KiB classes. The tables are immutable after construction.
BinnedSizeClasses::BinnedSizeClasses(std::uint32_t native_page_size)
    : page_size_(native_page_size) {
  BW_ALLOCATOR_CHECK(page_size_ >= 4096U && page_size_ % 4096U == 0U);
  std::uint32_t next = 0;
  const auto add = [&](std::uint32_t multiple, std::initializer_list<std::uint32_t> sizes) {
    std::uint32_t pages = 1;
    while ((pages * (page_size_ / 4096U)) % multiple != 0U) {
      ++pages;
    }
    for (const auto size : sizes) {
      classes_[next++] = {size, pages};
    }
  };
  add(1, {8,   16,  32,  48,  64,  80,  96,  112, 128, 144, 160,  176,  192,  224,
          256, 288, 320, 368, 400, 448, 512, 576, 672, 816, 1024, 1360, 2048, 4096});
  add(2, {736, 1168, 1632, 2720, 8192});
  add(3, {1536, 1744, 2448, 3072, 6144, 12288});
  add(4, {3264, 5456, 16384});
  add(5, {5120, 10240, 20480});
  add(6, {24576});
  add(7, {4768, 5728, 7168, 9552, 14336, 28672});
  BW_ALLOCATOR_CHECK(next == 52U);
  std::sort(classes_.begin(), classes_.begin() + next,
            [](const auto& left, const auto& right) { return left.bytes < right.bytes; });
  for (std::uint32_t size = 32768U; size <= binned_small_limit; size += 4096U) {
    add(size / 4096U, {size});
  }
  BW_ALLOCATOR_CHECK(next == binned_class_count);
  // One byte per 8-byte request quantum, first fitting bin.
  std::uint32_t pool = 0;
  for (std::size_t quantum = 0; quantum < lookup_.size(); ++quantum) {
    while (classes_[pool].bytes < quantum * 8U) {
      ++pool;
    }
    lookup_[quantum] = static_cast<std::uint8_t>(pool);
  }
}

// Checked descriptor access.
const BinnedSizeClass& BinnedSizeClasses::at(std::uint32_t index) const {
  BW_ALLOCATOR_CHECK(index < classes_.size());
  return classes_[index];
}

// Ceil request to lookup quantum.
std::uint32_t BinnedSizeClasses::index_for(std::size_t bytes) const {
  BW_ALLOCATOR_CHECK(bytes <= binned_small_limit);
  return lookup_[(bytes + 7U) >> 3U];
}

// Preserve small classes; larger alignment searches divisible bins, then falls
// through to native pages.
std::size_t BinnedSizeClasses::quantize(std::size_t bytes, std::uint32_t alignment) const {
  BW_ALLOCATOR_CHECK((alignment & (alignment - 1U)) == 0U);
  if (bytes <= binned_small_limit && alignment <= 16U) {
    return classes_[index_for(bytes)].bytes;
  }
  alignment = std::max(alignment, 16U);
  const auto requested = align_up(bytes, alignment);
  if (requested <= binned_small_limit && alignment <= 256U) {
    auto pool = index_for(requested);
    while (classes_[pool].bytes % alignment != 0U) {
      ++pool;
    }
    return classes_[pool].bytes;
  }
  return align_up(bytes, std::max(alignment, page_size_));
}

// Mutate only on success, except the initial minimum alignment adjustment, which
// also persists on failure.
bool BinnedSizeClasses::promote(std::size_t& bytes, std::uint32_t& alignment) const {
  alignment = std::max(alignment, 16U);
  const auto requested = align_up(bytes, alignment);
  if (requested > binned_small_limit || alignment > 256U) {
    return false;
  }
  auto pool = index_for(requested);
  while (classes_[pool].bytes % alignment != 0U) {
    ++pool;
  }
  bytes = classes_[pool].bytes;
  alignment = 16U;
  return true;
}
} // namespace bw::memory
