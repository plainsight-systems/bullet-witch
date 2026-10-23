// Memory migration: native owners propagate the ResourceService CPU domain to
// final pixel/glyph storage and retained presentation state/strings. Owning
// allocators retain the domain through copies and partial-construction cleanup;
// defaults are legacy parser/test compatibility, never exhaustion fallback.
// PNG writes directly to its final owned destination. Fixed draw storage adds
// no per-frame allocations; native texture lifetime stays with SpriteOutput.
// Design R.1 (#rr-raii), implementation E.6 (#re-raii), both-phase MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retain owners and
// fixed presentation capacity. Pinned source/external libpng exclusions are
// recorded in research/engine/subsystems/memory-image-presentation.md. Owned
// PNG image decoding for renderer texture construction. Input is borrowed only
// during decode; output owns top-to-bottom straight-alpha sRGB RGBA8 pixels.
// Reject malformed/unsupported inputs or caller limits with resources::Error;
// allocation failures propagate. No I/O, graphics handles or shared state.
// Algorithm: validate bounded PNG dimensions, delegate compression/filter/color
// conversion to pinned libpng, return independent pixels. O(encoded bytes +
// pixels), one output allocation plus bounded library state. See
// research/engine/subsystems/title-assets.md. Design: cpp-guidelines SL.con.3
// (#rsl-bounds) requires checked span/size limits; cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) retains implicit
// owning-value moves. Implementation: cpp-guidelines E.6 (#re-raii) releases
// libpng state on all paths; COPY.4 keeps the returned vector rule-of-zero.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

#include "bw/memory/allocator.hpp"
namespace bw::formats {
struct Image {
  std::uint32_t width{}, height{};
  memory::Vector<std::uint8_t> rgba;
};
inline constexpr std::uint32_t kMaximumImageDimension = 8192;
inline constexpr std::size_t kImageByteBudget = 64ULL * 1024 * 1024;
struct ImageLimits {
  std::uint32_t maximum_dimension{kMaximumImageDimension};
  std::size_t maximum_decoded_bytes{kImageByteBudget};
  std::size_t maximum_encoded_bytes{kImageByteBudget};
};
Image decode_png(std::span<const std::uint8_t> encoded, ImageLimits limits = {},
                 const memory::Allocator<std::uint8_t>& allocator = {});
}  // namespace bw::formats
