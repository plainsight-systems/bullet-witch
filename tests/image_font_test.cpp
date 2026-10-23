// In-process PNG/FONT contract checks using only synthetic owned bytes. Verify
// canonical RGBA, signed glyph metrics/channel mapping, ownership and malformed
// bounds. No files, renderer or process launches. Small fixture-sized cost.
// Design: cpp-guidelines SL.con.3 (#rsl-bounds) bounds fixture writes;
// cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps fixtures owning values.
// Implementation: E.6 (#re-raii) and COPY.4 release failed decode
// intermediates.
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>

#include "bw/formats/bitmap_font.hpp"
#include "bw/formats/common.hpp"
#include "bw/formats/image.hpp"
namespace {
void check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
void rejects(const std::function<void()>& decode) {
  try {
    decode();
  } catch (const bw::resources::Error&) {
    return;
  }
  throw std::runtime_error("Malformed fixture accepted");
}
void word(std::vector<std::uint8_t>& bytes, std::size_t at,
          std::uint32_t value) {
  for (unsigned byte = 0; byte < 4; ++byte) {
    bytes.at(at + byte) = static_cast<std::uint8_t>(value >> (byte * 8U));
  }
}
std::vector<std::uint8_t> font_fixture() {
  constexpr std::size_t pages = 48;
  constexpr std::size_t glyphs = pages + 256ULL * 12;
  std::vector<std::uint8_t> bytes(glyphs + 16 + 4);
  word(bytes, 0, 0x544e4f46);
  word(bytes, 4, 1);
  word(bytes, 8, 1);
  word(bytes, 12, 1);
  word(bytes, 20, 32);
  word(bytes, 24, 16);
  word(bytes, 32, 16);
  word(bytes, 36, 12);
  word(bytes, pages + 4, 1);
  word(bytes, glyphs, 1 | (2U << 16U));
  word(bytes, glyphs + 4, 8 | (10U << 16U));
  bytes.at(glyphs + 8) = 1;
  bytes.at(glyphs + 9) = 0xf8;
  bytes.at(glyphs + 10) = 8;
  bytes.at(glyphs + 11) = 0;
  bytes.at(glyphs + 12) = 2;
  bytes.at(glyphs + 13) = 9;
  word(bytes, glyphs + 16, 65);
  return bytes;
}
void font_contracts() {
  auto bytes = font_fixture();
  auto font = bw::formats::BitmapFont::parse(bytes);
  bytes.clear();
  const auto glyph = font.glyph(u'A');
  check(glyph.has_value() && glyph->top == 2 && glyph->bottom == 10 &&
            glyph->bearing_top == -8 && glyph->advance == 9 &&
            glyph->channel == bw::formats::FontChannel::green && !glyph->blank,
        "FONT glyph coordinates, signed metrics or channel lost");
  check(font.pixel_size() == 16 && font.baseline() == 12 &&
            font.atlas_width() == 16 && font.atlas_height() == 16 &&
            !font.glyph(u'B'),
        "FONT metrics or absent glyph incorrect");
  // Original mask identity is B/G/R/A, not the named-color interpretation of
  // the packed integer. Independent expected values cover every nonblank flag.
  using bw::formats::FontChannel;
  constexpr std::array<std::pair<std::uint8_t, FontChannel>, 4> channels{
      std::pair<std::uint8_t, FontChannel>{1, FontChannel::blue},
      {2, FontChannel::green},
      {4, FontChannel::red},
      {8, FontChannel::alpha}};
  for (const auto& [flag, expected] : channels) {
    bytes = font_fixture();
    bytes.at(48 + 256 * 12 + 12) = flag;
    const auto decoded = bw::formats::BitmapFont::parse(bytes).glyph(u'A');
    check(decoded && decoded->channel == expected,
          "Original FONT coverage channel was not converted to PNG RGBA");
  }
  bytes = font_fixture();
  bytes.pop_back();
  rejects([&] { (void)bw::formats::BitmapFont::parse(bytes); });
  bytes = font_fixture();
  word(bytes, 48 + 4, 2);
  rejects([&] { (void)bw::formats::BitmapFont::parse(bytes); });
  bytes = font_fixture();
  word(bytes, 48 + 8, 1);
  rejects([&] { (void)bw::formats::BitmapFont::parse(bytes); });
  bytes = font_fixture();
  word(bytes, 48 + 256 * 12 + 4, 17);
  rejects([&] { (void)bw::formats::BitmapFont::parse(bytes); });
  bytes = font_fixture();
  bytes.at(48 + 256 * 12 + 12) = 0x10;
  rejects([&] { (void)bw::formats::BitmapFont::parse(bytes); });
}
void image_contracts() {
  // Authored 1x1 PNG: pixel RGBA(10,20,30,40), generated from zlib-compressed
  // unfiltered scanline and standard PNG chunks; contains no game bytes.
  constexpr std::array<std::uint8_t, 70> png{
      137, 80, 78,  71,  13,  10,  26, 10, 0,  0,   0,   13,  73,  72,
      68,  82, 0,   0,   0,   1,   0,  0,  0,  1,   8,   6,   0,   0,
      0,   31, 21,  196, 137, 0,   0,  0,  13, 73,  68,  65,  84,  120,
      156, 99, 224, 18,  145, 211, 0,  0,  0,  205, 0,   101, 106, 153,
      132, 66, 0,   0,   0,   0,   73, 69, 78, 68,  174, 66,  96,  130};
  auto image = bw::formats::decode_png(png);
  check(image.width == 1 && image.height == 1 &&
            image.rgba == std::vector<std::uint8_t>{10, 20, 30, 40},
        "PNG did not preserve straight-alpha RGBA");
  rejects([&] { (void)bw::formats::decode_png(std::span(png).first(32)); });
  rejects([&] { (void)bw::formats::decode_png(png, {1, 3, 1024}); });
  auto damaged = png;
  damaged.at(50) ^= 0xff;
  rejects([&] { (void)bw::formats::decode_png(damaged); });
}
}  // namespace
int main() {
  try {
    image_contracts();
    font_contracts();
    std::cout << "PNG and bitmap-font contract checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
