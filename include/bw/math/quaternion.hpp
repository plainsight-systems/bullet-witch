// Shared Hamilton product, with quaternion components ordered x,y,z,w.
// Inputs/output are fixed owned values; no allocation, normalization, implicit
// clock or synchronization. Authored magnitude is retained. With the shared
// row-vector matrix convention, product(left,right) applies right then left.
// Nonfinite arithmetic propagates for the caller's validation policy.
// Both phases Core I.1 (#ri-explicit) names order/representation; performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps bounded
// storage.
#pragma once
#include <algorithm>

#include "vector.hpp"

namespace bw::math {
// Conjugate divided by squared norm; a caller-selected degeneracy threshold
// returns the zero quaternion. Fixed arithmetic, no normalization/allocation.
// Both phases Core R.14 and performance MEM.9 retain fixed-value inputs; the
// caller owns finite-input and threshold policy. Source shared-motion mapping
// uses float epsilon; other consumers need not inherit that threshold.
template <std::floating_point T>
constexpr Vector<T, 4> quaternion_inverse_or_zero(const Vector<T, 4>& value,
                                                  T minimum_squared_norm) {
  const T squared = accumulated_dot(value, value);
  if (squared <= minimum_squared_norm) return {};
  return {-value[0] / squared, -value[1] / squared, -value[2] / squared,
          value[3] / squared};
}
template <std::floating_point T>
struct QuaternionProduct {
  Vector<T, 4> left, right;
};
template <std::floating_point T>
constexpr Vector<T, 4> quaternion_product(const QuaternionProduct<T>& factors) {
  const auto [x, y, z, w] = factors.left;
  const auto [a, b, c, d] = factors.right;
  return {w * a + x * d + y * c - z * b, w * b - x * c + y * d + z * a,
          w * c + x * b - y * a + z * d, w * d - x * a - y * b - z * c};
}
// Shortest-path spherical interpolation with an explicit near-parallel
// threshold. Preserve authored magnitudes and component precision; no implicit
// normalization. Finite unit inputs give a unit result outside the linear
// branch. Caller supplies alpha/threshold in [0,1].
template <std::floating_point T>
struct SphericalBlend {
  T alpha, linear_threshold;
};
template <std::floating_point T>
Vector<T, 4> quaternion_slerp(const Vector<T, 4>& left,
                              const Vector<T, 4>& right,
                              SphericalBlend<T> blend) {
  const auto [alpha, linear_threshold] = blend;
  T alignment = accumulated_dot(left, right);
  const T sign = alignment < 0 ? T{-1} : T{1};
  alignment = std::clamp(alignment * sign, T{0}, T{1});
  T first = 1 - alpha, second = alpha;
  if (alignment < linear_threshold) {
    const T angle = std::acos(alignment);
    const T denominator = std::sqrt(1 - alignment * alignment);
    first = std::sin(first * angle) / denominator;
    second = std::sin(second * angle) / denominator;
  }
  Vector<T, 4> result{};
  for (std::size_t i = 0; i < result.size(); ++i) {
    result.at(i) = left.at(i) * first + right.at(i) * (second * sign);
  }
  return result;
}
// Halfway-vector quaternion construction, preserving input magnitudes. Only
// the sum is normalized: unit directions produce a rotation quaternion;
// nonunit inputs retain the source magnitude/alignment behavior. Finite inputs
// are caller-owned. Exact opposite sums use the supplied perpendicular axis;
// this primitive does not choose a fallback or normalize the output.
template <std::floating_point T>
struct DirectionRotation {
  Vector3<T> from, to, opposite_axis;
};
template <std::floating_point T>
Vector<T, 4> rotation_between(const DirectionRotation<T>& input) {
  const auto& [from, to, opposite_axis] = input;
  const auto sum = add(from, to);
  const T squared = dot(sum, sum);
  if (squared <= 0) {
    return {opposite_axis[0], opposite_axis[1], opposite_axis[2], 0};
  }
  const auto half = scale(sum, T{1} / std::sqrt(squared));
  const auto axis = cross(from, half);
  return {axis[0], axis[1], axis[2], dot(from, half)};
}
}  // namespace bw::math
