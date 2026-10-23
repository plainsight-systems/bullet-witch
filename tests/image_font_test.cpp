// Domain extension: decoded pixels/glyphs retain allocator capabilities through
// copies; malformed input and native/budget rejection unwind without fallback.
// R.1 (#rr-raii), E.6 (#re-raii), MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) apply as consulted in both
// design/implementation phases; no native rendering or filesystem dependency.
// In-process PNG/FONT contract checks using only synthetic owned bytes. Verify
// canonical RGBA, signed glyph metrics/channel mapping, ownership and malformed
// bounds. No files, renderer or process launches. Small fixture-sized cost.
// Design: cpp-guidelines SL.con.3 (#rsl-bounds) bounds fixture writes;
// cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps fixtures owning values.
// Implementation: E.6 (#re-raii) and COPY.4 release failed decode
// intermediates.
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>

#include "bw/formats/bitmap_font.hpp"
#include "bw/formats/common.hpp"
#include "bw/formats/image.hpp"
#include "bw/formats/world_texture.hpp"
#include "media_memory_fixture.hpp"
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
void font_memory_contracts() {
  const auto backing = std::make_shared<bw::test::MediaBacking>();
  std::weak_ptr<bw::memory::Domain> owner;
  std::optional<bw::formats::BitmapFont> retained;
  {
    auto domain = bw::memory::Domain::create(backing);
    owner = domain;
    const auto decoded = bw::formats::BitmapFont::parse(
        font_fixture(), bw::memory::Allocator<std::byte>(domain));
    retained = decoded;
    check(domain->snapshot().live_bytes > 0, "Font tables are domain owned");
  }
  check(!owner.expired() && retained->glyph(u'A').has_value(),
        "Copied font retains its domain after producer destruction");
  retained.reset();
  check(owner.expired() && backing->acquired == backing->released,
        "Final font owner returns all storage");
  auto domain = bw::memory::Domain::create(backing);
  auto damaged = font_fixture();
  word(damaged, 48 + 256 * 12 + 16, 65 | (1U << 16U));
  rejects([&] {
    (void)bw::formats::BitmapFont::parse(
        damaged, bw::memory::Allocator<std::byte>(domain));
  });
  check(domain->snapshot().live_allocations == 0,
        "Malformed mapping returns already-decoded glyph storage");
  backing->reject.store(true);
  bool failed = false;
  try {
    (void)bw::formats::BitmapFont::parse(
        font_fixture(), bw::memory::Allocator<std::byte>(domain));
  } catch (const std::bad_alloc&) {
    failed = true;
  }
  check(failed && domain->snapshot().live_allocations == 0,
        "Font allocation rejection never falls back to compatibility storage");
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
void world_texture_contracts() {
  // Authored4x2 RGBA byte ramp, no color-profile metadata or original bytes.
  constexpr std::array<std::uint8_t, 99> png{
      137, 80,  78, 71,  13, 10,  26, 10,  0,   0,   0,   13,  73,  72,  68,
      82,  0,   0,  0,   4,  0,   0,  0,   2,   8,   6,   0,   0,   0,   127,
      168, 125, 99, 0,   0,  0,   42, 73,  68,  65,  84,  120, 156, 99,  96,
      96,  100, 98, 102, 97, 101, 99, 231, 224, 228, 226, 230, 225, 229, 227,
      103, 16,  16, 20,  18, 22,  17, 21,  19,  151, 144, 148, 146, 150, 145,
      149, 147, 7,  0,   21, 234, 1,  241, 191, 137, 157, 82,  0,   0,   0,
      0,   73,  69, 78,  68, 174, 66, 96,  130};
  const auto domain =
      bw::memory::Domain::create(std::make_shared<bw::test::MediaBacking>());
  const bw::memory::Allocator<std::uint8_t> allocator(domain);
  std::vector<std::uint8_t> metadata(40);
  word(metadata, 32, 29);
  word(metadata, 36, 1 | (1U << 13U));
  {
    const auto decoded =
        bw::formats::decode_world_texture({png, metadata}, allocator);
    check(decoded.width == 2 && decoded.height == 2 &&
              decoded.format == bw::formats::WorldTextureFormat::rgba16_float,
          "Packed HDR uses metadata dimensions, not PNG width");
    for (std::size_t i = 0; i < decoded.pixels.size(); i += 4) {
      check(decoded.pixels[i] == i + 2 && decoded.pixels[i + 1] == i + 1 &&
                decoded.pixels[i + 2] == i && decoded.pixels[i + 3] == i + 3,
            "Packed HDR preserves original BGRA byte copy as little-endian "
            "HALF4");
    }
    word(metadata, 32, 18);
    const auto ordinary =
        bw::formats::decode_world_texture({png, metadata}, allocator);
    check(ordinary.width == 4 && ordinary.height == 2 &&
              ordinary.pixels[0] == 0 && ordinary.pixels[2] == 2 &&
              ordinary.format == bw::formats::WorldTextureFormat::rgba8,
          "Ordinary world texture retains RGBA color order");
  }
  word(metadata, 32, 29);
  // Six independent encoded faces retain their order in one HALF4 owner.
  constexpr std::size_t kFaceCount = 6;
  constexpr std::size_t kTableBytes = kFaceCount * 8;
  std::vector<std::uint8_t> cube(kTableBytes);
  for (std::size_t face = 0; face < kFaceCount; ++face) {
    word(cube, face * 4, static_cast<std::uint32_t>(cube.size()));
    word(cube, kFaceCount * 4 + face * 4,
         static_cast<std::uint32_t>(png.size()));
    cube.insert(cube.end(), png.begin(), png.end());
  }
  // Cube constructor takes only the packed width; height is square by contract.
  word(metadata, 36, 1);
  {
    const auto decoded = bw::formats::decode_world_texture(
        {cube, metadata, bw::formats::WorldTextureShape::cube}, allocator);
    check(decoded.shape == bw::formats::WorldTextureShape::cube &&
              decoded.width == 2 && decoded.height == 2 &&
              decoded.pixels.size() == kFaceCount * 32,
          "Cube preserves six square packed-HDR faces");
    for (std::size_t face = 0; face < kFaceCount; ++face) {
      check(decoded.pixels.at(face * 32) == 2 &&
                decoded.pixels.at(face * 32 + 2) == 0,
            "Cube face bytes keep original channel interpretation");
    }
  }
  word(cube, 4, kTableBytes);
  rejects([&] {
    (void)bw::formats::decode_world_texture(
        {cube, metadata, bw::formats::WorldTextureShape::cube}, allocator);
  });
  word(metadata, 36, 3 | (1U << 13U));
  rejects([&] {
    (void)bw::formats::decode_world_texture({png, metadata}, allocator);
  });
  metadata.resize(31);
  rejects([&] {
    (void)bw::formats::decode_world_texture({png, metadata}, allocator);
  });
  check(domain->snapshot().live_bytes == 0,
        "Failed world conversion releases all decoded pixels");
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
  const auto backing = std::make_shared<bw::test::MediaBacking>();
  std::weak_ptr<bw::memory::Domain> owner;
  std::optional<bw::formats::Image> retained;
  {
    const auto domain = bw::memory::Domain::create(backing);
    owner = domain;
    const auto decoded = bw::formats::decode_png(
        png, {}, bw::memory::Allocator<std::uint8_t>(domain));
    retained = decoded;
  }
  check(!owner.expired() && retained->rgba[3] == 40,
        "Copied PNG pixels retain their original domain");
  retained.reset();
  check(owner.expired() && backing->acquired == backing->released,
        "Final decoded image releases domain pixels");
  const auto limited =
      bw::memory::Domain::create(backing, {.max_live_bytes = 3});
  bool failed = false;
  try {
    (void)bw::formats::decode_png(png, {},
                                  bw::memory::Allocator<std::uint8_t>(limited));
  } catch (const std::bad_alloc&) {
    failed = true;
  }
  check(failed && limited->snapshot().live_allocations == 0,
        "PNG output budget rejects without global fallback or leaked charge");
  auto image = bw::formats::decode_png(png);
  check(image.width == 1 && image.height == 1 &&
            std::ranges::equal(image.rgba,
                               std::array<std::uint8_t, 4>{10, 20, 30, 40}),
        "PNG did not preserve straight-alpha RGBA");
  rejects([&] { (void)bw::formats::decode_png(std::span(png).first(32)); });
  rejects([&] { (void)bw::formats::decode_png(png, {1, 3, 1024}); });
  auto damaged = png;
  damaged.at(50) ^= 0xff;
  rejects([&] { (void)bw::formats::decode_png(damaged); });
  const auto domain = bw::memory::Domain::create(backing);
  rejects([&] {
    (void)bw::formats::decode_png(damaged, {},
                                  bw::memory::Allocator<std::uint8_t>(domain));
  });
  check(domain->snapshot().live_allocations == 0,
        "PNG codec failure releases the domain output buffer");
}
}  // namespace
int main() {
  try {
    image_contracts();
    world_texture_contracts();
    font_contracts();
    font_memory_contracts();
    std::cout << "PNG and bitmap-font contract checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
