// Prepared font -> bounded sprites, preserving original packed-channel glyphs.
// Borrow all inputs for the call, validate fully before writing, then emit.
// No resource/device authority or steady-state allocation. Core F.16 (#rf-in),
// SL.con.3 (#rsl-bounds), performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), both phases
// consulted.
#include "sprite_text.hpp"

#include <cmath>
#include <stdexcept>

namespace bw::game {
namespace {
render::TextureChannel channel(formats::FontChannel value) {
  switch (value) {
    case formats::FontChannel::red:
      return render::TextureChannel::red;
    case formats::FontChannel::green:
      return render::TextureChannel::green;
    case formats::FontChannel::blue:
      return render::TextureChannel::blue;
    case formats::FontChannel::alpha:
      return render::TextureChannel::alpha;
  }
  throw std::invalid_argument("Invalid original font channel");
}
}  // namespace
std::size_t append_sprite_text(const formats::BitmapFont& font,
                               render::TextureHandle atlas,
                               std::string_view text,
                               SpriteTextPosition position, render::Color color,
                               std::span<render::Sprite> output) {
  constexpr unsigned kAsciiLimit = 128;
  if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
      !std::isfinite(position.size) || position.size <= 0 || atlas.value == 0) {
    throw std::invalid_argument("Invalid sprite text placement");
  }
  std::size_t required = 0;
  for (const char character : text) {
    const auto codepoint = static_cast<unsigned char>(character);
    if (codepoint == '\n') continue;
    if (codepoint >= kAsciiLimit) {
      throw std::invalid_argument("Sprite text requires ASCII");
    }
    const auto glyph = font.glyph(codepoint);
    if (!glyph) throw std::runtime_error("Missing original font glyph");
    if (!glyph->blank) ++required;
  }
  if (required > output.size()) {
    throw std::length_error("Sprite text exceeds command capacity");
  }
  const auto scale = position.size / static_cast<float>(font.pixel_size());
  const auto atlas_width = static_cast<float>(font.atlas_width());
  const auto atlas_height = static_cast<float>(font.atlas_height());
  auto cursor = position.x;
  auto baseline = position.y + static_cast<float>(font.baseline()) * scale;
  std::size_t count = 0;
  for (const char character : text) {
    const auto codepoint = static_cast<unsigned char>(character);
    if (codepoint == '\n') {
      cursor = position.x;
      baseline += position.size;
      continue;
    }
    const auto found = font.glyph(codepoint);
    if (!found) throw std::logic_error("Validated original glyph missing");
    const auto& glyph = *found;
    if (!glyph.blank) {
      const auto width = static_cast<float>(glyph.right - glyph.left);
      const auto height = static_cast<float>(glyph.bottom - glyph.top);
      output[count++] = {
          atlas,
          {cursor + static_cast<float>(glyph.bearing_left) * scale,
           baseline + static_cast<float>(glyph.bearing_top) * scale,
           width * scale, height * scale},
          {static_cast<float>(glyph.left) / atlas_width,
           static_cast<float>(glyph.top) / atlas_height, width / atlas_width,
           height / atlas_height},
          color,
          channel(glyph.channel)};
    }
    cursor += static_cast<float>(glyph.advance) * scale;
  }
  return count;
}
}  // namespace bw::game
