#ifndef BW_RENDERING_SCENE_DETAIL_LEVEL_HPP
#define BW_RENDERING_SCENE_DETAIL_LEVEL_HPP

#include <cstdint>
#include <span>

// Responsibility: which detail level of a mesh an instance draws, from the
// size its bounding sphere projects to (docs/architecture/scene.md,
// invariant 13 and "Detail levels"). One rule, stated here and written
// twice: these functions for the CPU order of blended instances, and the
// culling dispatch's HLSL for opaque and masked ones, which adds the
// dithered transition between the levels two origins choose.
// The two levels come from two samples, the current and a lagged view
// origin. Every view that selects a level is a perspective view; shadow
// views draw the level the main view chose.
//
// Contract.
//   1. projected_radius_squared(center_distance_squared, radius,
//      projection_scale) = (projection_scale * radius)^2 /
//      max(1, center_distance_squared), with projection_scale as
//      frame_views.hpp gives it: the square of the fraction of half the
//      viewport the sphere's radius covers.
//   2. select_detail_level(screen_sizes, projected) is the highest index i
//      with (screen_sizes[i] / 2)^2 >= projected, scanning from the last
//      level down, and 0 when no level qualifies. screen_sizes holds one
//      entry per level, level 0 the most detailed. One level, or none, is
//      0 without a comparison.
//   3. detail_transition(newer, older, transition) is what the dispatch
//      draws from the levels the view's two detail origins choose
//      (frame_views.hpp, contract 5): when the two are equal, that level
//      alone with weight 0; otherwise both, `older` fading out with weight
//      `transition` and `newer` fading in with weight `transition - 1`,
//      transition in [0, 1]. A pixel of a level with weight w is kept when w is 0, when
//      w > 0 and w < r, or when w < 0 and w + 1 > r, r the pixel's dither
//      value in [0, 1): one of the two levels reaches each pixel.
//      Not built: no pixel stage applies the keep rule, so two levels of
//      one instance would both be drawn whole. No frame holds two: the
//      converter gives every mesh one level (content_converter/clusters.hpp,
//      contract 4). The rule is built with the first mesh that carries a
//      second level.
//
// Core guidelines read: I.1, F.6 (pure arithmetic, noexcept). The HLSL
// repeats the arithmetic of contracts 1 to 3; with one level per mesh no
// scenario compares the two.
namespace bw::scene {

inline constexpr std::uint32_t kMaxDetailLevels = 8;

[[nodiscard]] float projected_radius_squared(float center_distance_squared, float radius,
                                             float projection_scale) noexcept;

[[nodiscard]] std::uint32_t select_detail_level(std::span<const float> screen_sizes,
                                                float projected) noexcept;

struct DetailTransition {
  std::uint32_t first_level{};
  float first_weight{};
  // Equal to first_level when one level is drawn.
  std::uint32_t second_level{};
  float second_weight{};
};

[[nodiscard]] DetailTransition detail_transition(std::uint32_t newer, std::uint32_t older,
                                                 float transition) noexcept;

} // namespace bw::scene

#endif
