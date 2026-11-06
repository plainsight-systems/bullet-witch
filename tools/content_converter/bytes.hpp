#ifndef BW_CONTENT_CONVERTER_BYTES_HPP
#define BW_CONTENT_CONVERTER_BYTES_HPP

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// Responsibility: the little-endian reads every original-format reader in
// the converter shares. Private to the tool's library; no public header
// names it. Every read assembles its value from bytes (no pointer cast), and
// every caller has checked that the bytes are inside the span before it
// reads.
namespace bw::content_converter::bytes {

[[nodiscard]] inline std::uint32_t u8(std::span<const std::byte> data, std::size_t at) noexcept {
  return std::to_integer<std::uint32_t>(data[at]);
}

[[nodiscard]] inline std::uint32_t u16(std::span<const std::byte> data, std::size_t at) noexcept {
  return u8(data, at) | (u8(data, at + 1) << 8U);
}

[[nodiscard]] inline std::uint32_t u32(std::span<const std::byte> data, std::size_t at) noexcept {
  return u8(data, at) | (u8(data, at + 1) << 8U) | (u8(data, at + 2) << 16U) |
         (u8(data, at + 3) << 24U);
}

[[nodiscard]] inline std::int32_t i32(std::span<const std::byte> data, std::size_t at) noexcept {
  return std::bit_cast<std::int32_t>(u32(data, at));
}

[[nodiscard]] inline float f32(std::span<const std::byte> data, std::size_t at) noexcept {
  return std::bit_cast<float>(u32(data, at));
}

// Four bytes as stored.
[[nodiscard]] inline std::array<char, 4> tag(std::span<const std::byte> data,
                                             std::size_t at) noexcept {
  return {std::to_integer<char>(data[at]), std::to_integer<char>(data[at + 1]),
          std::to_integer<char>(data[at + 2]), std::to_integer<char>(data[at + 3])};
}

// Four bytes stored last-first, read back in reading order (the model
// header's chunk and parameter tags).
[[nodiscard]] inline std::array<char, 4> reversed_tag(std::span<const std::byte> data,
                                                      std::size_t at) noexcept {
  return {std::to_integer<char>(data[at + 3]), std::to_integer<char>(data[at + 2]),
          std::to_integer<char>(data[at + 1]), std::to_integer<char>(data[at])};
}

[[nodiscard]] inline bool within(std::uint64_t offset, std::uint64_t size,
                                 std::uint64_t limit) noexcept {
  return offset <= limit && size <= limit - offset;
}

// The NUL-terminated string at `offset` inside `block`, or found false when
// the offset is outside the block or no NUL follows inside it.
struct CString {
  std::string_view text{};
  bool found{};
};
[[nodiscard]] inline CString c_string(std::span<const std::byte> block,
                                      std::uint64_t offset) noexcept {
  if (offset >= block.size()) {
    return {};
  }
  for (std::size_t end = static_cast<std::size_t>(offset); end < block.size(); ++end) {
    if (block[end] == std::byte{0}) {
      // Viewing bytes as characters is the one cast the language allows for
      // this (char may alias any object).
      return {std::string_view(reinterpret_cast<const char*>(block.data()) + offset,
                               end - static_cast<std::size_t>(offset)),
              true};
    }
  }
  return {};
}

} // namespace bw::content_converter::bytes

#endif
