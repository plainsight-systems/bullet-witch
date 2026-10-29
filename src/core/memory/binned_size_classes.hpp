#ifndef BW_CORE_MEMORY_BINNED_SIZE_CLASSES_HPP
#define BW_CORE_MEMORY_BINNED_SIZE_CLASSES_HPP

#include <array>
#include <cstddef>
#include <cstdint>

// Performance MEM.5: separate small size classes from native-page allocations.
// Inspected source values, not the guideline's illustrative class boundaries.
namespace bw::memory {
inline constexpr std::uint32_t binned_class_count = 77;
inline constexpr std::size_t binned_small_limit = std::size_t{128} * 1024U;
inline constexpr std::uint64_t binned_reservation_per_class = 1ULL << 30U;

struct BinnedSizeClass {
  std::uint32_t bytes{};
  std::uint32_t pages_per_block{};
};

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b:
// HAL/MallocBinnedCommon.cpp:27-165; .h:823-894; MallocBinned3.cpp:475-543.
// Immutable after construction; native page size is supplied by the allocator.
class BinnedSizeClasses final {
public:
  // Build the original listed classes and block multiples, then append to 128 KiB.
  explicit BinnedSizeClasses(std::uint32_t native_page_size);
  [[nodiscard]] const BinnedSizeClass& at(std::uint32_t index) const;
  // Small requests only, including zero: round lookup to 8-byte index granularity.
  [[nodiscard]] std::uint32_t index_for(std::size_t bytes) const;
  // Default alignment0 retains the source 8-byte minimum class. Higher alignment
  // may promote through256; otherwise round to native pages for the large path.
  [[nodiscard]] std::size_t quantize(std::size_t bytes, std::uint32_t alignment) const;
  // Source PromoteToLargerBin: on success rewrite size and standard alignment;
  // failure retains the source's alignment adjustment, without allocating.
  [[nodiscard]] bool promote(std::size_t& bytes, std::uint32_t& alignment) const;

private:
  std::uint32_t page_size_{};
  std::array<BinnedSizeClass, binned_class_count> classes_{};
  std::array<std::uint8_t, 1U + (binned_small_limit >> 3U)> lookup_{};
};
} // namespace bw::memory

#endif
