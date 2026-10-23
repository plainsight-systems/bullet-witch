// Owned original FONT metadata decoding; compatibility identifiers stay here.
// Input is borrowed only during parse. Glyph lookup returns independent values;
// immutable owners support concurrent reads. No file, renderer or font
// fallback. Malformed bounds/order/coordinates throw resources::Error;
// unsupported flags fail explicitly. Algorithm: validate header,
// faces/pages/glyphs/map ranges, decode signed bearings and channel masks,
// build ordered Unicode lookup. Lookup is O(log glyph count); parse O(bytes +
// glyphs), two owned vectors. See research/engine/subsystems/title-assets.md.
// Design: cpp-guidelines SL.con.3 (#rsl-bounds) checks every byte range;
// cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) preserves implicit
// moves. Implementation: E.6 (#re-raii) and COPY.4 compose vector owners so
// failures release partial decoding without custom destruction.
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>
namespace bw::formats {
// Canonical PNG channel identity, already converted from original FONT flags.
// Design/implementation: I.1 (#ri-explicit) makes channel meaning explicit;
// CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// converts the legacy mask once at load. Original flags 1/2/4/8 map B/G/R/A.
enum class FontChannel : std::uint8_t { red, green, blue, alpha };
struct FontGlyph {
  std::uint16_t left{}, top{}, right{}, bottom{};
  std::int8_t bearing_left{}, bearing_top{}, bearing_right{}, bearing_bottom{};
  std::int8_t advance{};
  FontChannel channel{FontChannel::red};
  bool blank{};
};
class BitmapFont {
 public:
  static BitmapFont parse(std::span<const std::uint8_t> encoded);
  std::optional<FontGlyph> glyph(char16_t codepoint) const;
  std::uint32_t pixel_size() const { return pixel_size_; }
  std::uint32_t baseline() const { return baseline_; }
  std::uint32_t atlas_width() const { return atlas_width_; }
  std::uint32_t atlas_height() const { return atlas_height_; }

 private:
  std::uint32_t pixel_size_{}, baseline_{}, atlas_width_{}, atlas_height_{};
  std::vector<FontGlyph> glyphs_;
  std::vector<std::pair<char16_t, std::uint32_t>> lookup_;
};
}  // namespace bw::formats
