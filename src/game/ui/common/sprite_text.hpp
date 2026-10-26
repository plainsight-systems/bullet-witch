// Shared prepared-font sprite emission. Borrow font, atlas identity, text and
// bounded caller-owned commands only for this call; retain nothing and do not
// submit output or mutate device/resource owners. The caller supplies logical
// text geometry and handles publication. Algorithm: validate BMP UTF-8/glyphs
// and capacity, then map bearings/channel/UV into ordered quads. No parsing,
// loads or allocation during emission. Core F.16 (#rf-in), SL.con.3
// (#rsl-bounds), performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) consulted in both
// phases; reuse one prepared atlas without copies. Formatting follows the
// existing original-font menu adapter; gameplay-hud.md.
#pragma once
#include <span>
#include <string_view>

#include "bw/content/formats/bitmap_font.hpp"
#include "bw/render/output/sprites.hpp"

namespace bw::game {
struct SpriteTextPosition {
  float x{}, y{}, size{};
};
// Strict UTF-8 decoder for original BMP font text. Cursor advances only on
// success; malformed/unsupported code points throw. No retained state.
char16_t next_sprite_character(std::string_view text, std::size_t& cursor);
// Returns count appended. The supplied output range starts at the first free
// command. Invalid characters/missing glyphs/capacity throw before any writes.
std::size_t append_sprite_text(const formats::BitmapFont& font,
                               render::TextureHandle atlas,
                               std::string_view text,
                               SpriteTextPosition position, render::Color color,
                               std::span<render::Sprite> output);
}  // namespace bw::game
