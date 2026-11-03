#ifndef BW_MATH_ROTATION_QUATERNION_INL
#define BW_MATH_ROTATION_QUATERNION_INL

#include "bw/diagnostics/math_diagnostic.hpp"
#include "bw/platform/math/math.hpp"

#include <array>

// Read C.49/F.20/GEN.6: preserve owned values, the declared initialization and
// inline visibility without a blanket force-inline policy.
namespace bw::math {

// Force XYZ zero and choose W zero; no diagnostic.
template <typename T> inline Quaternion4<T>::Quaternion4(ZeroInit) : x{0}, y{0}, z{0}, w{0} {}

// The identity tag differs only in W.
template <typename T> inline Quaternion4<T>::Quaternion4(IdentityInit) : x{0}, y{0}, z{0}, w{1} {}

// Initialize every member from the scalar, then diagnose.
template <typename T>
inline Quaternion4<T>::Quaternion4(T value) : x{value}, y{value}, z{value}, w{value} {
  diagnostic_check();
}

// Initialize corresponding XYZW members before the diagnostic.
// Components deliberately retain their ordered same-type signature.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
template <typename T>
inline Quaternion4<T>::Quaternion4(T in_x, T in_y, T in_z, T in_w)
    : x{in_x}, y{in_y}, z{in_z}, w{in_w} {
  diagnostic_check();
}

// Explicit component casts delegate to the diagnosing constructor.
template <typename T>
template <typename U>
  requires(!std::is_same_v<T, U>)
inline Quaternion4<T>::Quaternion4(const Quaternion4<U>& other)
    : Quaternion4{static_cast<T>(other.x), static_cast<T>(other.y), static_cast<T>(other.z),
                  static_cast<T>(other.w)} {}

// Store the register directly into contiguous aligned XYZW.
// Intentionally initializes by the native store rather than member zeros.
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
template <typename T> inline Quaternion4<T>::Quaternion4(Register value) {
  simd::store_aligned(value, &x);
  diagnostic_check();
}

// Expose the protected register constructor through a value factory.
template <typename T> inline Quaternion4<T> Quaternion4<T>::from_register(Register value) {
  return Quaternion4{value};
}

// Aligned inputs, native product, store, diagnose.
template <typename T>
inline Quaternion4<T> Quaternion4<T>::operator*(const Quaternion4& other) const {
  const auto first = simd::load_aligned(&x);
  const auto second = simd::load_aligned(&other.x);
  // The destination is completely initialized by the following native store.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  Quaternion4 result;
  simd::store_aligned(simd::quaternion_multiply(first, second), &result.x);
  result.diagnostic_check();
  return result;
}

// Load both before writing, allowing self-alias; return a copy.
template <typename T> inline Quaternion4<T> Quaternion4<T>::operator*=(const Quaternion4& other) {
  const auto first = simd::load_aligned(&x);
  const auto second = simd::load_aligned(&other.x);
  simd::store_aligned(simd::quaternion_multiply(first, second), &x);
  diagnostic_check();
  return *this;
}

// Native length/tolerance comparison followed by candidate+select.
// Default is a promoted float literal; supplied T is unchanged.
template <typename T> inline void Quaternion4<T>::normalize(T squared_tolerance) {
  const auto value = simd::load_aligned(&x);
  const auto squared_length = simd::dot4(value, value);
  const auto usable =
      simd::compare_greater_equal(squared_length, simd::broadcast(squared_tolerance));
  // Evaluated even for zero/NaN. No positivity guard, early-out or diagnostic.
  const auto inverse_length = simd::reciprocal_square_root(squared_length);
  const auto candidate = simd::multiply(inverse_length, value);
  // Identity lanes (0,0,0,1) convert exactly to either precision. Immutable local
  // storage spells that register constant without loading the dynamically
  // initialized value identity.
  static constexpr std::array<T, 4> identity_lanes{T{0}, T{0}, T{0}, T{1}};
  simd::store_aligned(simd::select(usable, candidate, simd::load(identity_lanes.data())), &x);
}

// Normalize an implicit copy, leaving the receiver unchanged.
template <typename T> inline Quaternion4<T> Quaternion4<T>::normalized(T squared_tolerance) const {
  Quaternion4 result{*this};
  result.normalize(squared_tolerance);
  return result;
}

// Negate any-greater on absolute error, not a finite predicate.
template <typename T> inline bool Quaternion4<T>::is_normalized() const {
  const auto value = simd::load_aligned(&x);
  const auto error =
      simd::absolute(simd::subtract(simd::broadcast(T{1}), simd::dot4(value, value)));
  // The threshold is float 0.01f, including its rounding for double.
  return !simd::any_greater(error, simd::broadcast(static_cast<T>(0.01F)));
}

// Retain scalar left-associated XYZW sum, not register dot4.
template <typename T> inline T Quaternion4<T>::dot(const Quaternion4& other) const {
  return x * other.x + y * other.y + z * other.z + w * other.w;
}

// Scalar sum of squares in component order.
template <typename T> inline T Quaternion4<T>::length_squared() const {
  return x * x + y * y + z * z + w * w;
}

// The selected scalar platform square root of that same sum.
template <typename T> inline T Quaternion4<T>::length() const {
  return platform::math::square_root(x * x + y * y + z * z + w * w);
}

// Scalar Vector3 cross/add/scale even with native intrinsics enabled.
template <typename T> inline Vector3<T> Quaternion4<T>::rotate(Vector3<T> vector) const {
  const Vector3<T> imaginary{x, y, z};
  // T = 2.f * cross(Q,V), then V + W*T + cross(Q,T), left-associated.
  // Existing member scale spells scalar-left multiplication with identical arithmetic.
  const auto twice_crossed = imaginary.cross(vector) * 2.F;
  return vector + twice_crossed * w + imaginary.cross(twice_crossed);
}

// Same scalar sequence with negated imaginary components.
template <typename T> inline Vector3<T> Quaternion4<T>::unrotate(Vector3<T> vector) const {
  const Vector3<T> imaginary{-x, -y, -z};
  const auto twice_crossed = imaginary.cross(vector) * 2.F;
  return vector + twice_crossed * w + imaginary.cross(twice_crossed);
}

// Short-circuit scalar finite checks in XYZW order, all builds.
template <typename T> inline bool Quaternion4<T>::contains_non_finite() const {
  return !platform::math::is_finite(x) || !platform::math::is_finite(y) ||
         !platform::math::is_finite(z) || !platform::math::is_finite(w);
}

// Nonfatal once-only Debug report.
// Exceptional once-state is unsynchronized; preserve every component.
template <typename T> inline void Quaternion4<T>::diagnostic_check() const {
#if BW_ENABLE_MATH_DIAGNOSTICS
  if (contains_non_finite()) {
    static bool reported{false};
    if (!reported) {
      diagnostics::log_quaternion_non_finite(static_cast<double>(x), static_cast<double>(y),
                                             static_cast<double>(z), static_cast<double>(w));
      reported = true;
    }
  }
#endif
}
} // namespace bw::math
#endif // BW_MATH_ROTATION_QUATERNION_INL
