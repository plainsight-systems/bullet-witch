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
// recorded in research/engine/subsystems/memory-image-presentation.md. Channel
// correction design/implementation: cpp-guidelines I.1 (#ri-explicit) specifies
// canonical PNG RGBA semantics at this boundary. cpp-performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// converts original packed-channel identity once while parsing; renderer draws
// remain independent of legacy mask words. Original pixel inspection, not only
// mapping bounds, validates the conversion (title-assets.md).
// Original FONT metadata decoder; contract in bw/formats/bitmap_font.hpp.
// Validate all sections before allocating glyph/map vectors, then decode each
// bounded record and ordered codepoint mapping. Lookup returns an owned glyph.
// Costs: O(glyphs + mappings + 256) parse, O(log mappings) lookup; no runtime
// I/O. Design: cpp-guidelines SL.con.3 (#rsl-bounds) checks section arithmetic;
// cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps result owners movable.
// Implementation: E.6 (#re-raii) and COPY.4 release partial vectors on errors.
// Compatibility evidence: 0x41a2b0 section layout, 0x41a250 page lookup,
// 0x413130 glyph coordinates/masks; research/engine/subsystems/title-assets.md.
#include "bw/formats/bitmap_font.hpp"

#include <algorithm>
#include <bit>

#include "reader.hpp"
namespace bw::formats {
namespace {
constexpr unsigned kByteBits = 8;
constexpr std::uint8_t kAlphaFlag = 8;
constexpr std::uint8_t kBlankFlag = 64;
constexpr std::uint32_t kMaximumFaces = 256;
constexpr std::uint32_t kCodepointCount = 65536;
constexpr std::uint32_t kMaximumMetric = 256;
constexpr std::uint32_t kMaximumAtlasDimension = 8192;
constexpr std::size_t kAtlasWidthOffset = 24;
constexpr std::size_t kAtlasHeightOffset = 20;
constexpr std::size_t kBottomOffset = 6;
constexpr std::size_t kBearingLeftOffset = 8;
constexpr std::size_t kBearingTopOffset = 9;
constexpr std::size_t kBearingRightOffset = 10;
constexpr std::size_t kBearingBottomOffset = 11;
constexpr std::size_t kAdvanceOffset = 13;
std::uint16_t half(const resources::detail::Reader& reader,
                   std::size_t offset) {
  reader.range(offset, 2);
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(reader.b[offset]) |
      (static_cast<std::uint16_t>(reader.b[offset + 1]) << kByteBits));
}
// The original mask words 0x00ff0000/0x0000ff00/0x000000ff select
// PNG B/G/R, respectively. Convert to canonical channel names here; treating
// these packed mask words as ARGB colors selects unrelated atlas glyphs.
FontChannel channel(std::uint8_t flags) {
  switch (flags) {
    case 1:
      return FontChannel::blue;
    case 2:
      return FontChannel::green;
    case 4:
      return FontChannel::red;
    case kAlphaFlag:
      return FontChannel::alpha;
    case kBlankFlag:
      return FontChannel::red;
    default:
      resources::detail::unsupported("Unsupported FONT glyph flags");
  }
}
}  // namespace
BitmapFont BitmapFont::parse(std::span<const std::uint8_t> encoded,
                             const memory::Allocator<std::byte>& allocator) {
  using resources::detail::malformed;
  constexpr std::size_t header_bytes = 32;
  constexpr std::size_t face_bytes = 16;
  constexpr std::size_t page_bytes = 12;
  constexpr std::size_t page_count = 256;
  constexpr std::size_t glyph_bytes = 16;
  constexpr std::size_t mapping_bytes = 4;
  const resources::detail::Reader reader{encoded};
  reader.range(0, header_bytes);
  if (reader.tag(0) != "FONT") {
    malformed("Not a FONT resource");
  }
  const auto mappings = reader.u32(4);
  const auto faces = reader.u32(8);
  const auto glyph_count = reader.u32(12);
  if (faces == 0 || faces > kMaximumFaces || mappings > kCodepointCount ||
      glyph_count > kCodepointCount) {
    malformed("FONT counts exceed format bounds");
  }
  const auto pages =
      header_bytes + static_cast<std::size_t>(faces) * face_bytes;
  const auto glyphs = pages + page_count * page_bytes;
  const auto lookup =
      glyphs + static_cast<std::size_t>(glyph_count) * glyph_bytes;
  const auto end = lookup + static_cast<std::size_t>(mappings) * mapping_bytes;
  reader.range(0, end);
  if (end != encoded.size()) {
    malformed("FONT section size mismatch");
  }
  BitmapFont output;
  output.glyphs_ = memory::Vector<FontGlyph>(allocator);
  output.lookup_ =
      memory::Vector<std::pair<char16_t, std::uint32_t>>(allocator);
  output.pixel_size_ = reader.u32(header_bytes);
  output.baseline_ = reader.u32(header_bytes + 4);
  output.atlas_width_ = reader.u32(kAtlasWidthOffset);
  output.atlas_height_ = reader.u32(kAtlasHeightOffset) / 2;
  if (!output.pixel_size_ || output.pixel_size_ > kMaximumMetric ||
      output.baseline_ > kMaximumMetric || !output.atlas_width_ ||
      !output.atlas_height_ || output.atlas_width_ > kMaximumAtlasDimension ||
      output.atlas_height_ > kMaximumAtlasDimension ||
      (reader.u32(kAtlasHeightOffset) & 1U) != 0) {
    malformed("Invalid FONT metrics or atlas dimensions");
  }
  output.glyphs_.reserve(glyph_count);
  for (std::size_t index = 0; index < glyph_count; ++index) {
    const auto offset = glyphs + index * glyph_bytes;
    const auto flags = encoded[offset + 12];
    FontGlyph glyph{
        half(reader, offset),
        half(reader, offset + 2),
        half(reader, offset + 4),
        half(reader, offset + kBottomOffset),
        std::bit_cast<std::int8_t>(encoded[offset + kBearingLeftOffset]),
        std::bit_cast<std::int8_t>(encoded[offset + kBearingTopOffset]),
        std::bit_cast<std::int8_t>(encoded[offset + kBearingRightOffset]),
        std::bit_cast<std::int8_t>(encoded[offset + kBearingBottomOffset]),
        std::bit_cast<std::int8_t>(encoded[offset + kAdvanceOffset]),
        channel(flags),
        flags == kBlankFlag};
    if (glyph.left > glyph.right || glyph.top > glyph.bottom ||
        glyph.right > output.atlas_width_ ||
        glyph.bottom > output.atlas_height_ ||
        glyph.bearing_left > glyph.bearing_right ||
        glyph.bearing_top > glyph.bearing_bottom) {
      malformed("FONT glyph rectangle exceeds bounds");
    }
    output.glyphs_.push_back(glyph);
  }
  output.lookup_.reserve(mappings);
  std::size_t mapped{};
  for (std::size_t page = 0; page < page_count; ++page) {
    const auto offset = pages + page * page_bytes;
    const auto first = reader.u32(offset);
    const auto count = reader.u32(offset + 4);
    const auto glyph_base = reader.u32(offset + 8);
    if (first > mappings || count > mappings - first ||
        (count != 0 && first != mapped)) {
      malformed("FONT codepoint page exceeds mapping table");
    }
    for (std::size_t index = 0; index < count; ++index) {
      const auto entry = lookup + (first + index) * mapping_bytes;
      const auto codepoint = half(reader, entry);
      const auto relative = half(reader, entry + 2);
      if ((codepoint >> kByteBits) != page || glyph_base >= glyph_count ||
          relative >= glyph_count - glyph_base ||
          (!output.lookup_.empty() &&
           output.lookup_.back().first >= codepoint)) {
        malformed("Invalid or unordered FONT glyph mapping");
      }
      output.lookup_.emplace_back(static_cast<char16_t>(codepoint),
                                  glyph_base + relative);
    }
    mapped += count;
  }
  if (mapped != mappings) {
    malformed("Unmapped FONT codepoint entries");
  }
  return output;
}
std::optional<FontGlyph> BitmapFont::glyph(char16_t codepoint) const {
  const auto found = std::lower_bound(
      lookup_.begin(), lookup_.end(), codepoint,
      [](const auto& entry, char16_t key) { return entry.first < key; });
  if (found == lookup_.end() || found->first != codepoint) {
    return std::nullopt;
  }
  return glyphs_.at(found->second);
}
}  // namespace bw::formats
