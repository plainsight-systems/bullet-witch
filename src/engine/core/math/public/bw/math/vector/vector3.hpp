#ifndef BW_CORE_MATH_VECTOR3_HPP
#define BW_CORE_MATH_VECTOR3_HPP

#include <type_traits>

#ifndef BW_ENABLE_MATH_DIAGNOSTICS
#error "Consume math through the bw_math target for consistent inline configuration."
#endif

// Read guidance: F.20 returns independent numeric values; ES.23 makes caller
// initialization/conversion explicit. SIMD.1/3 keep silicon kernels behind the
// platform contract; this source TVector tier itself uses scalar expressions.
// Reference pin: f45729c0b6f097038e72b52d366a62baeca3275b.
namespace bw::math {

// Our spelling of Unreal's EForceInit construction choice (Vector.h:1514–1519).
struct ZeroInit {};
inline constexpr ZeroInit zero_init{};

template <typename T> struct Vector3 {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);

  // UE Vector.h:49–76: three independent components, without deprecated union
  // alias or SIMD padding. Do not read a default-initialized component.
  T x;
  T y;
  T z;

  // UE TVector() Vector.h:149–150: preserve ordinary uninitialized construction.
  // Braced value-initialization still follows C++ zero-initialization semantics.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  Vector3() = default;
  // UE TVector(InF) Vector.h:1471–1476: set all components, then diagnostic.
  explicit Vector3(T value);
  // UE TVector(X,Y,Z) Vector.h:1484–1489: initialize XYZ, then diagnostic.
  Vector3(T in_x, T in_y, T in_z);
  // UE TVector(EForceInit) Vector.h:1514–1519: zero XYZ, then diagnostic.
  explicit Vector3(ZeroInit);
  // UE cross-type TVector Vector.h:1214–1216: explicitly cast each component.
  template <typename U>
    requires(!std::is_same_v<T, U>)
  explicit Vector3(const Vector3<U>& other);

  // UE ZeroVector Vector.h:78–79: immutable per-precision zero value.
  static const Vector3 zero;

  // UE TVector::operator+ Vector.h:1562–1566: component sum, value result.
  [[nodiscard]] Vector3 operator+(const Vector3& other) const;
  // UE TVector::operator- Vector.h:1568–1572: component difference.
  [[nodiscard]] Vector3 operator-(const Vector3& other) const;
  // UE TVector::operator* Vector.h:1574–1577: component-wise product.
  [[nodiscard]] Vector3 operator*(const Vector3& other) const;
  // UE TVector::operator* Vector.h:311–315: cast arithmetic scale to T, scale XYZ.
  template <typename U>
    requires std::is_arithmetic_v<U>
  [[nodiscard]] Vector3 operator*(U scale) const;
  // UE TVector::operator/ Vector.h:323–328: source reciprocal then scale XYZ.
  // No divide-by-zero assertion, clamping or alternate reciprocal policy.
  template <typename U>
    requires std::is_arithmetic_v<U>
  [[nodiscard]] Vector3 operator/(U scale) const;
  // UE TVector unary operator- Vector.h:1610–1615: negate XYZ.
  [[nodiscard]] Vector3 operator-() const;
  // UE TVector::operator== Vector.h:1586–1590: exact component equality.
  [[nodiscard]] bool operator==(const Vector3& other) const;

  // UE TVector::Dot / operator| Vector.h:1544–1559: XYZ product sum.
  [[nodiscard]] T dot(const Vector3& other) const;
  // UE TVector::Cross / operator^ Vector.h:1521–1542: determinant orientation.
  [[nodiscard]] Vector3 cross(const Vector3& other) const;
  // UE TVector::SizeSquared Vector.h:1725–1729: square XYZ and sum.
  [[nodiscard]] T length_squared() const;
  // UE TVector::Size Vector.h:1713–1717: sqrt of squared component sum.
  [[nodiscard]] T length() const;
  // UE TVector::Normalize Vector.h:1764–1775: strict squared-length > tolerance;
  // true means scale in place. Failure leaves receiver untouched. The default
  // is the source float literal converted to T even for the double variant.
  bool normalize(T squared_tolerance = static_cast<T>(1.e-8F));
  // UE TVector::GetSafeNormal Vector.h:2057–2073: exact unit length first;
  // squared length below tolerance returns fallback unchanged; else normalize.
  // Source accepts equality here, unlike in-place Normalize.
  [[nodiscard]] Vector3 safe_normal(T squared_tolerance = static_cast<T>(1.e-8F),
                                    const Vector3& fallback = zero) const;
  // UE TVector::ContainsNaN Vector.h:2293–2299: NaN OR infinity in any component.
  [[nodiscard]] bool contains_non_finite() const;

private:
  // UE TVector::DiagnosticCheckNaN Vector.h:128–147 and
  // LogMacros.h:400–422; CoreMisc.cpp:503–522 at the pin above:
  // Debug checks nonfinite; source-default false ensure flag logs once per
  // precision/callsite and returns unchanged.
  void diagnostic_check() const;
};

// UE MathFwd.h:46–79: keep both precise variants; ordinary Vector alias double.
using Vector3f = Vector3<float>;
using Vector3d = Vector3<double>;
using Vector = Vector3d;

// UE UnrealMath.cpp:33,44 explicitly defines these constants through XYZ
// construction. Declarations name the selected specializations.
template <> const Vector3<float> Vector3<float>::zero;
template <> const Vector3<double> Vector3<double>::zero;

} // namespace bw::math

#include "bw/math/vector/vector3.inl"

#endif // BW_CORE_MATH_VECTOR3_HPP
