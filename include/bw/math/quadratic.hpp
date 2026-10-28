// Real roots of a scalar quadratic using the direct ordered formula. Finite
// coefficients are caller preconditions. Return minus-root then plus-root;
// linear equations return their single root twice, and no-real/constant
// equations return no result. This explicit arithmetic order lets compatibility
// consumers retain source rounding rather than silently selecting a different
// numerical algorithm. Fixed storage, no allocation or synchronization.
// Core SL.con.3 (#rsl-bounds) returns a bounded pair; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps scalar work
// fixed. Both corpora consulted in the native-action design/implementation
// phase.
#pragma once
#include <array>
#include <cmath>
#include <concepts>
#include <optional>
namespace bw::math {
template <std::floating_point T>
std::optional<std::array<T, 2>> quadratic_roots(T a, T b, T c) {
  if (a == 0) {
    if (b == 0) return std::nullopt;
    const T root = -c / b;
    return std::array<T, 2>{root, root};
  }
  const T discriminant = b * b - a * T{4} * c;
  if (discriminant < 0) return std::nullopt;
  const T root = static_cast<T>(std::sqrt(static_cast<double>(discriminant)));
  return std::array<T, 2>{(-b - root) / (a * T{2}), (root - b) / (a * T{2})};
}
}  // namespace bw::math
