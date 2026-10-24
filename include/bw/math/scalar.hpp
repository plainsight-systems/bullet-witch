// Portable scalar interpolation with explicit source operation order. Pure,
// allocation-free value arithmetic; caller chooses/clamps the finite factor.
// No extrapolation or validation policy is hidden. Core I.1 (#ri-explicit) and
// Performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keep
// precision and fixed cost explicit; both phases consulted. Unlike std::lerp,
// this preserves a + factor*(b-a) ordering for recovered animation sampling.
#pragma once
#include <concepts>
namespace bw::math {
template <std::floating_point T>
constexpr T linear_interpolate(T first, T last, T factor) {
  return factor * (last - first) + first;
}
}  // namespace bw::math
