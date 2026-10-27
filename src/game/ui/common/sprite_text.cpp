// Prepared font -> bounded sprites, preserving original packed-channel glyphs.
// Borrow all inputs for the call, validate fully before writing, then emit.
// No resource/device authority or steady-state allocation. Core F.16 (#rf-in),
// SL.con.3 (#rsl-bounds), performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), both phases
// consulted.
#include "sprite_text.hpp"

#include <algorithm>
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
char16_t next_sprite_character(std::string_view text, std::size_t& cursor) {
  if (cursor >= text.size()) {
    throw std::invalid_argument("Missing sprite text character");
  }
  auto next_cursor = cursor;
  const auto first = static_cast<unsigned char>(text[next_cursor++]);
  constexpr unsigned kAscii = 128, kThreeStart = 0xe0, kFourStart = 0xf0,
                     kTwoStart = 0xc2, kThreeMask = 15, kTwoMask = 31,
                     kContinuationMask = 0xc0, kPayload = 63, kShift = 6,
                     kThreeMinimum = 0x800, kSurrogateFirst = 0xd800,
                     kSurrogateLast = 0xdfff;
  if (first < kAscii) {
    cursor = next_cursor;
    return first;
  }
  const unsigned count = first >= kThreeStart && first < kFourStart  ? 2U
                         : first >= kTwoStart && first < kThreeStart ? 1U
                                                                     : 0U;
  if (count == 0 || text.size() - next_cursor < count) {
    throw std::invalid_argument("Invalid original font UTF-8");
  }
  unsigned value = first & (count == 2 ? kThreeMask : kTwoMask);
  for (unsigned i = 0; i < count; ++i) {
    const auto byte = static_cast<unsigned char>(text[next_cursor++]);
    if ((byte & kContinuationMask) != kAscii) {
      throw std::invalid_argument("Invalid font UTF-8 continuation");
    }
    value = (value << kShift) | (byte & kPayload);
  }
  if (value < (count == 2 ? kThreeMinimum : kAscii) ||
      (value >= kSurrogateFirst && value <= kSurrogateLast)) {
    throw std::invalid_argument("Invalid font Unicode scalar");
  }
  cursor = next_cursor;
  return static_cast<char16_t>(value);
}
namespace {
std::size_t validate_glyphs(const formats::BitmapFont& font,
                            std::string_view text, std::size_t capacity) {
  std::size_t required = 0;
  for (std::size_t cursor = 0; cursor < text.size();) {
    const auto codepoint = next_sprite_character(text, cursor);
    if (codepoint == '\n') continue;
    const auto glyph = font.glyph(codepoint);
    if (!glyph) throw std::runtime_error("Missing original font glyph");
    if (!glyph->blank) ++required;
  }
  if (required > capacity) {
    throw std::length_error("Sprite text exceeds command capacity");
  }
  return required;
}
}  // namespace
std::size_t append_sprite_text(const formats::BitmapFont& font,
                               render::TextureHandle atlas,
                               std::string_view text,
                               SpriteTextPosition position, render::Color color,
                               std::span<render::Sprite> output) {
  if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
      !std::isfinite(position.size) || position.size <= 0 || atlas.value == 0) {
    throw std::invalid_argument("Invalid sprite text placement");
  }
  (void)validate_glyphs(font, text, output.size());
  const auto scale = position.size / static_cast<float>(font.pixel_size());
  const auto atlas_width = static_cast<float>(font.atlas_width());
  const auto atlas_height = static_cast<float>(font.atlas_height());
  auto cursor = position.x;
  auto baseline = position.y + static_cast<float>(font.baseline()) * scale;
  std::size_t count = 0;
  for (std::size_t text_cursor = 0; text_cursor < text.size();) {
    const auto codepoint = next_sprite_character(text, text_cursor);
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
std::size_t append_centered_sprite_text(const formats::BitmapFont& font,
                                        render::TextureHandle atlas,
                                        std::string_view text,
                                        SpriteTextPosition position,
                                        render::Color color,
                                        std::span<render::Sprite> output) {
  (void)validate_glyphs(font, text, output.size());
  constexpr float kHalf = .5F;
  const auto lines = 1 + std::ranges::count(text, '\n');
  position.y -= static_cast<float>(lines) * position.size * kHalf;
  const auto scale = position.size / static_cast<float>(font.pixel_size());
  std::size_t count = 0, offset = 0;
  while (offset <= text.size()) {
    const auto newline = text.find('\n', offset);
    const auto end = newline == std::string_view::npos ? text.size() : newline;
    const auto line = text.substr(offset, end - offset);
    float width = 0;
    for (std::size_t cursor = 0; cursor < line.size();) {
      const auto glyph = font.glyph(next_sprite_character(line, cursor));
      if (!glyph) throw std::runtime_error("Missing original font glyph");
      width += static_cast<float>(glyph->advance) * scale;
    }
    count += append_sprite_text(
        font, atlas, line,
        {position.x - width * kHalf, position.y, position.size}, color,
        output.subspan(count));
    position.y += position.size;
    if (newline == std::string_view::npos) break;
    offset = newline + 1;
  }
  return count;
}
}  // namespace bw::game
