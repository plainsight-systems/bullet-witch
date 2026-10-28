// Shared point-to-segment projection. Inputs are finite vectors; a degenerate
// segment returns its first endpoint. Nondegenerate projection preserves the
// source reciprocal-multiply order, clamps to the closed segment, then measures
// squared residual distance. Values own their components; no allocation,
// mutation, clock or synchronization. Constant fixed-vector work. Source
// distance helper551b30 is mapped in first-combat-physics-boundary.md.
// Core I.1 (#ri-explicit) exposes degeneracy; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps math inline and allocation-free.
// Both corpora read during design and implementation.
#pragma once
#include <algorithm>

#include "vector.hpp"
namespace bw::math {
template <std::floating_point T>
constexpr T point_segment_distance_squared(const Vector3<T>& point,
                                           const Vector3<T>& start,
                                           const Vector3<T>& end) {
  const auto direction = subtract(end, start);
  const auto offset = subtract(point, start);
  const auto denominator = dot(direction, direction);
  if (denominator == 0) return dot(offset, offset);
  const T parameter =
      std::clamp((T{1} / denominator) * dot(direction, offset), T{0}, T{1});
  const auto residual = subtract(offset, scale(direction, parameter));
  return dot(residual, residual);
}
}  // namespace bw::math
