#ifndef BW_CORE_MATH_VECTOR3_INL
#define BW_CORE_MATH_VECTOR3_INL

#include "bw/diagnostics/math_diagnostic.hpp"
#include "bw/platform/math/math.hpp"

// Read F.20/ES.23 and SIMD.1/3: preserve value operations and visible definitions;
// do not make each tiny numeric operation an out-of-line dispatch.

namespace bw::math {

// Vector3<T>::Vector3(T value)
// Initialize x/y/z from value; invoke diagnostic_check after initialization.
template <typename T> inline Vector3<T>::Vector3(T value) : x{value}, y{value}, z{value} {
  diagnostic_check();
}

// Vector3<T>::Vector3(T in_x, T in_y, T in_z)
// Initialize the three corresponding members; invoke diagnostic_check.
template <typename T>
inline Vector3<T>::Vector3(T in_x, T in_y, T in_z) : x{in_x}, y{in_y}, z{in_z} {
  diagnostic_check();
}

// Vector3<T>::Vector3(ZeroInit)
// Initialize all components to precision-appropriate zero; invoke diagnostic_check.
template <typename T> inline Vector3<T>::Vector3(ZeroInit) : x{T{0}}, y{T{0}}, z{T{0}} {
  diagnostic_check();
}

// Vector3<T>::Vector3(const Vector3<U>& other)
// Explicitly convert all three components to T and delegate to XYZ construction.
// Do not implicitly narrow ordinary arithmetic across vector precisions.
template <typename T>
template <typename U>
  requires(!std::is_same_v<T, U>)
inline Vector3<T>::Vector3(const Vector3<U>& other)
    : Vector3{static_cast<T>(other.x), static_cast<T>(other.y), static_cast<T>(other.z)} {}

// Vector3<T>::operator+(const Vector3& other) const
// Construct a result from the three component sums; result construction diagnoses.
template <typename T> inline Vector3<T> Vector3<T>::operator+(const Vector3& other) const {
  return Vector3{x + other.x, y + other.y, z + other.z};
}

// Vector3<T>::operator-(const Vector3& other) const
// Construct a result from the three component differences.
template <typename T> inline Vector3<T> Vector3<T>::operator-(const Vector3& other) const {
  return Vector3{x - other.x, y - other.y, z - other.z};
}

// Vector3<T>::operator*(const Vector3& other) const
// Construct a result from the three component-wise products.
template <typename T> inline Vector3<T> Vector3<T>::operator*(const Vector3& other) const {
  return Vector3{x * other.x, y * other.y, z * other.z};
}

// Vector3<T>::operator*(U scale) const
// Convert arithmetic scale to T before each multiplication; construct scaled XYZ.
template <typename T>
template <typename U>
  requires std::is_arithmetic_v<U>
inline Vector3<T> Vector3<T>::operator*(U scale) const {
  const T converted{static_cast<T>(scale)};
  return Vector3{x * converted, y * converted, z * converted};
}

// Vector3<T>::operator/(U scale) const
// Compute reciprocal as T(1) / scale and assign it to T by ordinary conversion.
// Preserve mixed arithmetic promotion in that division; multiply XYZ by reciprocal.
// Do not replace with three divisions or add a zero guard.
template <typename T>
template <typename U>
  requires std::is_arithmetic_v<U>
inline Vector3<T> Vector3<T>::operator/(U scale) const {
  // Make the usual arithmetic conversions explicit for
  // -Wconversion. Narrow only AFTER dividing in the promoted expression type.
  using Calculation = decltype(T{1} / scale);
  const T reciprocal{static_cast<T>(Calculation{1} / static_cast<Calculation>(scale))};
  return Vector3{x * reciprocal, y * reciprocal, z * reciprocal};
}

// Vector3<T>::operator-() const
// Construct a vector by negating each component.
template <typename T> inline Vector3<T> Vector3<T>::operator-() const {
  return Vector3{-x, -y, -z};
}

// Vector3<T>::operator==(const Vector3& other) const
// Return conjunction of the three exact comparisons; no epsilon equality.
template <typename T> inline bool Vector3<T>::operator==(const Vector3& other) const {
  return x == other.x && y == other.y && z == other.z;
}

// Vector3<T>::dot(const Vector3& other) const
// Return the left-associated sum of X products, Y products and Z products.
template <typename T> inline T Vector3<T>::dot(const Vector3& other) const {
  return x * other.x + y * other.y + z * other.z;
}

// Vector3<T>::cross(const Vector3& other) const
// Construct determinant components in XYZ cyclic order; X cross Y yields positive Z.
template <typename T> inline Vector3<T> Vector3<T>::cross(const Vector3& other) const {
  return Vector3{y * other.z - z * other.y, z * other.x - x * other.z, x * other.y - y * other.x};
}

// Vector3<T>::length_squared() const
// Return X squared plus Y squared plus Z squared, in that expression order.
template <typename T> inline T Vector3<T>::length_squared() const { return x * x + y * y + z * z; }

// Vector3<T>::length() const
// Compute squared component sum and call platform::math::square_root at precision T.
// No robust-hypot rescaling or float intermediary is introduced.
template <typename T> inline T Vector3<T>::length() const {
  return platform::math::square_root(x * x + y * y + z * z);
}

// Vector3<T>::normalize(T squared_tolerance)
// Compute squared length; only when strictly greater than tolerance:
//   obtain platform inverse square root, multiply each existing component, return true.
// Otherwise return false without changing any component. No extra diagnostic call.
template <typename T> inline bool Vector3<T>::normalize(T squared_tolerance) {
  const T squared{x * x + y * y + z * z};
  if (squared > squared_tolerance) {
    // Scale the existing receiver only after passing threshold.
    const T scale{platform::math::inverse_square_root(squared)};
    x *= scale;
    y *= scale;
    z *= scale;
    return true;
  }
  return false;
}

// Vector3<T>::safe_normal(T squared_tolerance, const Vector3& fallback) const
// Compute squared length. Exact equality to one returns original, before any tolerance check.
// If squared length is below tolerance, return fallback without normalization or modification.
// Otherwise obtain platform inverse square root and construct the scaled result.
template <typename T>
inline Vector3<T> Vector3<T>::safe_normal(T squared_tolerance, const Vector3& fallback) const {
  const T squared{x * x + y * y + z * z};
  // Unit branch has priority even if tolerance exceeds one.
  if (squared == T{1}) {
    return *this;
  }
  if (squared < squared_tolerance) {
    return fallback;
  }
  // Equality to tolerance reaches normal scaling.
  const T scale{platform::math::inverse_square_root(squared)};
  return Vector3{x * scale, y * scale, z * scale};
}

// Vector3<T>::contains_non_finite() const
// Return true if any component fails platform::math::is_finite; short-circuit XYZ.
template <typename T> inline bool Vector3<T>::contains_non_finite() const {
  return !platform::math::is_finite(x) || !platform::math::is_finite(y) ||
         !platform::math::is_finite(z);
}

// Vector3<T>::diagnostic_check() const
// In BW_ENABLE_MATH_DIAGNOSTICS Debug selection, check contains_non_finite.
// Use one local static bool for this diagnostic callsite per T; if not yet
// reported, call the concrete reporter with widened XYZ, then mark reported.
// Do not mark before publishing the error.
// Release/non-Debug selection compiles this check away. This is intended
// conditional behavior, not an unimplemented diagnostic hidden behind a no-op.
// Keep the exceptional once-state policy explicit; no invented throw/clamp or
// configurable function pointer.
template <typename T> inline void Vector3<T>::diagnostic_check() const {
#if BW_ENABLE_MATH_DIAGNOSTICS
  if (contains_non_finite()) {
    // Publish once, then remember that this callsite reported.
    static bool reported{false};
    if (!reported) {
      diagnostics::log_vector_non_finite(static_cast<double>(x), static_cast<double>(y),
                                         static_cast<double>(z));
      reported = true;
    }
  }
#endif
}

} // namespace bw::math

#endif // BW_CORE_MATH_VECTOR3_INL
