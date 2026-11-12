#include "bw/rendering/scene/detail_level.hpp"

#include <algorithm>
#include <cstdint>
#include <span>

// Responsibility: the out-of-line part of bw/rendering/scene/detail_level.hpp.
//
// Reading of the header that the code encodes: in detail_transition's two
// levels, `first` is the older origin's level, fading out, and `second` the
// newer's, fading in; the header's prose names them in that order.
namespace bw::scene {

// No factor from the projection's [2][3] element scales the squared
// distance: it is 1 for every perspective view, and no orthographic view
// selects a level.
// The three scalars in the order the header documents.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
float projected_radius_squared(float center_distance_squared, float radius,
                               float projection_scale) noexcept {
  const float scaled = projection_scale * radius;
  return (scaled * scaled) / std::max(1.0F, center_distance_squared);
}

// Scans from the last level, comparing against half the level's screen
// size, squared. No setting scales a level, and every cooked level carries
// its size.
std::uint32_t select_detail_level(std::span<const float> screen_sizes, float projected) noexcept {
  if (screen_sizes.size() <= 1U) {
    return 0;
  }
  for (std::size_t i = screen_sizes.size(); i > 0U; --i) {
    const float half = screen_sizes[i - 1U] * 0.5F;
    if (half * half >= projected) {
      return static_cast<std::uint32_t>(i - 1U);
    }
  }
  return 0;
}

// The older sample's level fades out with the transition value, the newer
// one fades in with the value minus one.
DetailTransition detail_transition(std::uint32_t newer, std::uint32_t older,
                                   float transition) noexcept {
  if (newer == older) {
    return DetailTransition{newer, 0.0F, newer, 0.0F};
  }
  return DetailTransition{older, transition, newer, transition - 1.0F};
}

} // namespace bw::scene
