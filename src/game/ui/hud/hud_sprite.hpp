// Value-only native HUD sprite request. No texture/resource ownership and no
// borrowed storage; the surface resolves original IDs from its resident art.
// Geometry is logical1280x720. Canvas pivot selects the original ring center;
// otherwise scale/rotation use the image center. Source native sprite draw
// contract: gameplay-hud.md. Core F.16 (#rf-in), SL.con.3 (#rsl-bounds) and
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// consulted in both phases: bounded owner arrays publish const command spans.
#pragma once
#include "bw/render/output/sprites.hpp"
namespace bw::game {
struct HudSprite {
  unsigned image{};
  render::Rect destination;
  render::Rect uv{0, 0, 1, 1};
  render::Color color{};
  float rotation_radians{};
  bool centered{};
  float scale{1};
  bool canvas_pivot{};
  bool additive{};
  bool top_left_pivot{};
};
}  // namespace bw::game
