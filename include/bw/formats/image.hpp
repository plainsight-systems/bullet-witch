// Owned PNG image decoding for renderer texture construction. Input is borrowed
// only during decode; output owns top-to-bottom straight-alpha sRGB RGBA8
// pixels. Reject malformed/unsupported inputs or caller limits with
// resources::Error; allocation failures propagate. No I/O, graphics handles or
// shared state. Algorithm: validate bounded PNG dimensions, delegate
// compression/filter/color conversion to pinned libpng, return independent
// pixels. O(encoded bytes + pixels), one output allocation plus bounded library
// state. See research/engine/subsystems/title-assets.md. Design: cpp-guidelines
// SL.con.3
// (#rsl-bounds) requires checked span/size limits; cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) retains implicit
// owning-value moves. Implementation: cpp-guidelines E.6 (#re-raii) releases
// libpng state on all paths; COPY.4 keeps the returned vector rule-of-zero.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace bw::formats {
struct Image {
  std::uint32_t width{}, height{};
  std::vector<std::uint8_t> rgba;
};
inline constexpr std::uint32_t kMaximumImageDimension = 8192;
inline constexpr std::size_t kImageByteBudget = 64ULL * 1024 * 1024;
struct ImageLimits {
  std::uint32_t maximum_dimension{kMaximumImageDimension};
  std::size_t maximum_decoded_bytes{kImageByteBudget};
  std::size_t maximum_encoded_bytes{kImageByteBudget};
};
Image decode_png(std::span<const std::uint8_t> encoded,
                 ImageLimits limits = {});
}  // namespace bw::formats
