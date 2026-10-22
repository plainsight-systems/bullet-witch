// A1 internal checked byte reader. Span borrows its decoder owner. Rejects
// out-of-range/unterminated input with Error. Algorithm: subtract bound before
// slicing, decode little-endian values. O(string length), O(1) scalar access.
// Optimization: spans avoid copying. No host file operations or
// synchronization. Design: cpp-guidelines I.1 (#ri-explicit), C.122
// (#rh-separation): explicit ownership and injected file capability.
// cpp-performance COPY.2
// (guidelines/copy-move/COPY.2-mark-move-constructors-noexcept.md): container
// owners retain implicit moves. Implementation: SL.con.3 (#rsl-bounds) checks
// slices before access; COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps owning values rule-of-zero.

#pragma once
#include <algorithm>

#include "bw/formats/common.hpp"
namespace bw::resources::detail {
constexpr unsigned kByteBits = 8;
constexpr std::uint64_t kWordBytes = 4;
constexpr std::uint64_t kByteOrderOffset = 8;
constexpr std::uint32_t kNoIndex = 0xffffffff;
[[noreturn]] inline void malformed(const std::string& message) {
  throw Error(ErrorCode::malformed, message);
}
[[noreturn]] inline void unsupported(const std::string& message) {
  throw Error(ErrorCode::unsupported, message);
}
struct Reader {
  std::span<const std::uint8_t> b;
  void range(std::uint64_t offset, std::uint64_t size) const {
    if (offset > b.size() || size > b.size() - offset) {
      malformed("Range exceeds file at offset " + std::to_string(offset));
    }
  }
  std::uint32_t u32(std::uint64_t offset) const {
    range(offset, 4);
    auto p = b.subspan(static_cast<std::size_t>(offset), sizeof(std::uint32_t));
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << kByteBits) |
           (static_cast<std::uint32_t>(p[2]) << (2 * kByteBits)) |
           (static_cast<std::uint32_t>(p[3]) << (3 * kByteBits));
  }
  std::string tag(std::uint64_t offset) const {
    range(offset, kWordBytes);
    const auto value = b.subspan(static_cast<std::size_t>(offset), kWordBytes);
    return {value.begin(), value.end()};
  }
  std::string text(Range pool, std::uint32_t offset) const {
    range(pool.offset, pool.size);
    if (offset >= pool.size) {
      malformed("String offset exceeds pool");
    }
    auto first = b.begin() + static_cast<std::ptrdiff_t>(pool.offset + offset);
    auto last =
        b.begin() + static_cast<std::ptrdiff_t>(pool.offset + pool.size);
    auto stop = std::find(first, last, 0);
    if (stop == last) {
      malformed("Unterminated pool string");
    }
    return {first, stop};
  }
  std::optional<std::string> optional_text(Range pool,
                                           std::uint32_t offset) const {
    return offset ? std::optional(text(pool, offset)) : std::nullopt;
  }
  void within(Range section, std::uint64_t offset, std::uint64_t size) const {
    if (offset > section.size || size > section.size - offset) {
      malformed("Range exceeds section at offset " +
                std::to_string(section.offset + offset));
    }
    range(section.offset + offset, size);
  }
};
}  // namespace bw::resources::detail
