// Shared prepared-font sprite emission. Borrow font, atlas identity, text and
// bounded caller-owned commands only for this call; retain nothing and do not
// submit output or mutate device/resource owners. The caller supplies logical
// text geometry and handles publication. Algorithm: validate ASCII/glyphs and
// capacity, then map bearings/channel/UV into ordered quads. No parsing, loads
// or allocation during emission. Core F.16 (#rf-in), SL.con.3 (#rsl-bounds),
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// consulted in both phases; reuse one prepared atlas without copies. Formatting
// follows the existing original-font menu adapter; gameplay-hud.md.
#pragma once
#include <span>
#include <string_view>

#include "bw/formats/bitmap_font.hpp"
#include "bw/render/sprites.hpp"

namespace bw::game {
struct SpriteTextPosition {
  float x{}, y{}, size{};
};
// Returns count appended. The supplied output range starts at the first free
// command. Invalid characters/missing glyphs/capacity throw before any writes.
std::size_t append_sprite_text(const formats::BitmapFont& font,
                               render::TextureHandle atlas,
                               std::string_view text,
                               SpriteTextPosition position, render::Color color,
                               std::span<render::Sprite> output);
}  // namespace bw::game
