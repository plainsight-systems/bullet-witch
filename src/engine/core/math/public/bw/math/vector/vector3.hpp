#ifndef BW_CORE_MATH_VECTOR3_HPP
#define BW_CORE_MATH_VECTOR3_HPP

#include <type_traits>

#ifndef BW_ENABLE_MATH_DIAGNOSTICS
#error "Consume math through the bw_math target for consistent inline configuration."
#endif

// Read guidance: F.20 returns independent numeric values; ES.23 makes caller
// initialization/conversion explicit. SIMD.1/3 keep silicon kernels behind the
// platform contract; this vector tier itself uses scalar expressions.
namespace bw::math {

// Tag selecting zero construction.
struct ZeroInit {};
inline constexpr ZeroInit zero_init{};

template <typename T> struct Vector3 {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);

  // Three independent components, without union alias or SIMD padding.
  // Do not read a default-initialized component.
  T x;
  T y;
  T z;

  // Preserve ordinary uninitialized construction.
  // Braced value-initialization still follows C++ zero-initialization semantics.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  Vector3() = default;
  // Set all components, then diagnostic.
  explicit Vector3(T value);
  // Initialize XYZ, then diagnostic.
  Vector3(T in_x, T in_y, T in_z);
  // Zero XYZ, then diagnostic.
  explicit Vector3(ZeroInit);
  // Cross-precision conversion: explicitly cast each component.
  template <typename U>
    requires(!std::is_same_v<T, U>)
  explicit Vector3(const Vector3<U>& other);

  // Immutable per-precision zero value.
  static const Vector3 zero;

  // Component sum, value result.
  [[nodiscard]] Vector3 operator+(const Vector3& other) const;
  // Component difference.
  [[nodiscard]] Vector3 operator-(const Vector3& other) const;
  // Component-wise product.
  [[nodiscard]] Vector3 operator*(const Vector3& other) const;
  // Cast arithmetic scale to T, scale XYZ.
  template <typename U>
    requires std::is_arithmetic_v<U>
  [[nodiscard]] Vector3 operator*(U scale) const;
  // Reciprocal then scale XYZ.
  // No divide-by-zero assertion, clamping or alternate reciprocal policy.
  template <typename U>
    requires std::is_arithmetic_v<U>
  [[nodiscard]] Vector3 operator/(U scale) const;
  // Negate XYZ.
  [[nodiscard]] Vector3 operator-() const;
  // Exact component equality.
  [[nodiscard]] bool operator==(const Vector3& other) const;

  // XYZ product sum.
  [[nodiscard]] T dot(const Vector3& other) const;
  // Determinant orientation.
  [[nodiscard]] Vector3 cross(const Vector3& other) const;
  // Square XYZ and sum.
  [[nodiscard]] T length_squared() const;
  // Sqrt of squared component sum.
  [[nodiscard]] T length() const;
  // Strict squared-length > tolerance;
  // true means scale in place. Failure leaves receiver untouched. The default
  // is a float literal converted to T even for the double variant.
  bool normalize(T squared_tolerance = static_cast<T>(1.e-8F));
  // Exact unit length first;
  // squared length below tolerance returns fallback unchanged; else normalize.
  // Accepts equality here, unlike in-place normalize.
  [[nodiscard]] Vector3 safe_normal(T squared_tolerance = static_cast<T>(1.e-8F),
                                    const Vector3& fallback = zero) const;
  // NaN OR infinity in any component.
  [[nodiscard]] bool contains_non_finite() const;

private:
  // Debug checks nonfinite components and logs once per precision/callsite,
  // leaving the values unchanged.
  void diagnostic_check() const;
};

// Keep both precise variants; ordinary Vector alias double.
using Vector3f = Vector3<float>;
using Vector3d = Vector3<double>;
using Vector = Vector3d;

// These constants are defined explicitly through XYZ construction.
// Declarations name the selected specializations.
template <> const Vector3<float> Vector3<float>::zero;
template <> const Vector3<double> Vector3<double>::zero;

} // namespace bw::math

#ifndef BW_CORE_MATH_VECTOR3_INL
#include "bw/math/vector/vector3.inl"
#endif

#endif // BW_CORE_MATH_VECTOR3_HPP
