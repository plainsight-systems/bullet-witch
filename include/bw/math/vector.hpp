// Shared fixed-vector arithmetic; numerical policy is independent of callers.
// Floating-point input type is retained in every operation. Arrays are owned
// values with no allocation, aliasing, mutable globals or synchronization.
// Operations consume components and return values; dot3 evaluates left to
// right without a zero seed, while accumulated_dot explicitly starts at zero.
// Ordinary IEEE arithmetic propagates nonfinite values; callers own validation,
// normalization thresholds and fallback direction. divide requires a nonzero
// divisor when finite output is needed. O(N) fixed local work/storage; no
// virtual dispatch or target-specific approximation. See shared-math.md.
// Both design/implementation: Core I.9 (#ri-concepts) constrains numeric types;
// I.1 (#ri-explicit) makes precision/order explicit. Performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps math
// allocation-free.
#pragma once
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>

namespace bw::math {
template <std::floating_point T, std::size_t Size>
using Vector = std::array<T, Size>;
template <std::floating_point T>
using Vector3 = Vector<T, 3>;

// Preserve component precision and operation order at this common boundary.
template <std::floating_point T, std::size_t Size>
constexpr Vector<T, Size> add(const Vector<T, Size>& a,
                              const Vector<T, Size>& b) {
  Vector<T, Size> result{};
  for (std::size_t i = 0; i < Size; ++i) result.at(i) = a.at(i) + b.at(i);
  return result;
}
template <std::floating_point T, std::size_t Size>
constexpr Vector<T, Size> subtract(const Vector<T, Size>& a,
                                   const Vector<T, Size>& b) {
  Vector<T, Size> result{};
  for (std::size_t i = 0; i < Size; ++i) result.at(i) = a.at(i) - b.at(i);
  return result;
}
template <std::floating_point T, std::size_t Size>
constexpr Vector<T, Size> scale(const Vector<T, Size>& a, T factor) {
  Vector<T, Size> result{};
  for (std::size_t i = 0; i < Size; ++i) result.at(i) = a.at(i) * factor;
  return result;
}
template <std::floating_point T, std::size_t Size>
constexpr Vector<T, Size> divide(const Vector<T, Size>& a, T divisor) {
  Vector<T, Size> result{};
  for (std::size_t i = 0; i < Size; ++i) result.at(i) = a.at(i) / divisor;
  return result;
}
template <std::floating_point T>
constexpr T dot(const Vector3<T>& a, const Vector3<T>& b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
// Shared squared separation preserves the existing subtraction/dot precision.
template <std::floating_point T>
constexpr T squared_distance(const Vector3<T>& a, const Vector3<T>& b) {
  const auto offset = subtract(a, b);
  return dot(offset, offset);
}
template <std::floating_point T, std::size_t Size>
constexpr T accumulated_dot(const Vector<T, Size>& a,
                            const Vector<T, Size>& b) {
  T result = 0;
  for (std::size_t i = 0; i < Size; ++i) result += a.at(i) * b.at(i);
  return result;
}
// Reflect an incident direction across a caller-supplied unit normal. No
// normalization or policy repair; fixed arithmetic, shared by query consumers.
// F.7/E.4 and MEM.9 were retrieved for this bounded value-only addition.
template <std::floating_point T>
constexpr Vector3<T> reflect(const Vector3<T>& incident,
                             const Vector3<T>& unit_normal) {
  return subtract(incident,
                  scale(unit_normal, T{2} * dot(incident, unit_normal)));
}
template <std::floating_point T>
constexpr Vector3<T> cross(const Vector3<T>& a, const Vector3<T>& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
          a[0] * b[1] - a[1] * b[0]};
}
template <std::floating_point T>
T length(const Vector3<T>& value) {
  return std::sqrt(dot(value, value));
}
// Normalize only above the caller's nonnegative length threshold; otherwise
// return its fallback unchanged. NaN magnitude also selects the fallback;
// infinite magnitude follows IEEE division. No default direction is invented.
template <std::floating_point T>
Vector3<T> normalize_or(const Vector3<T>& value, const Vector3<T>& fallback,
                        T minimum_length = 0) {
  const auto magnitude = length(value);
  return magnitude > minimum_length ? divide(value, magnitude) : fallback;
}
template <std::floating_point T, std::size_t Size>
bool all_finite(const Vector<T, Size>& value) {
  for (const auto component : value) {
    if (!std::isfinite(component)) return false;
  }
  return true;
}
}  // namespace bw::math
