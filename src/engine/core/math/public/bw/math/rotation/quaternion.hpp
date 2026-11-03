#ifndef BW_MATH_ROTATION_QUATERNION_HPP
#define BW_MATH_ROTATION_QUATERNION_HPP

#include "bw/math/rotation/quaternion_register.hpp"
#include "bw/math/vector/vector3.hpp"

#include <type_traits>

// Read C.49/F.20/GEN.6: explicit member construction, owned return values and
// header-visible algorithms. No-init default and native-store construction are
// intentional exceptions to imposing a uniform initialization algorithm.
namespace bw::math {
// Tag selecting identity construction: XYZ zero, W one.
struct IdentityInit {};
inline constexpr IdentityInit identity_init{};

template <typename T> struct alignas(16) Quaternion4 {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);
  using Register = std::conditional_t<std::is_same_v<T, float>, simd::Register4f, simd::Register4d>;

  // Owned contiguous XYZW values, align16; ordinary default leaves them uninitialized.
  T x;
  T y;
  T z;
  T w;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  Quaternion4() = default;

  // Force XYZ zero, W zero or one; no diagnostic.
  explicit Quaternion4(ZeroInit);
  explicit Quaternion4(IdentityInit);
  // Initialized components followed by diagnostic.
  explicit Quaternion4(T value);
  Quaternion4(T in_x, T in_y, T in_z, T in_w);
  // Cast each component and delegate to component constructor.
  template <typename U>
    requires(!std::is_same_v<T, U>)
  explicit Quaternion4(const Quaternion4<U>& other);

  // Construct through the protected native-register constructor.
  [[nodiscard]] static Quaternion4 from_register(Register value);
  // Explicit float/double const identity specializations.
  static const Quaternion4 identity;

  // Native product then diagnostic; A*B applies B then A.
  [[nodiscard]] Quaternion4 operator*(const Quaternion4& other) const;
  // Returns a value copy after mutation, not a reference.
  Quaternion4 operator*=(const Quaternion4& other);

  // Native squared-length >= caller tolerance, compute normalized candidate
  // then select identity. No added diagnostic. The default tolerance is a float
  // literal even when converted to double. No tolerance guard.
  void normalize(T squared_tolerance = static_cast<T>(1.e-8F));
  [[nodiscard]] Quaternion4 normalized(T squared_tolerance = static_cast<T>(1.e-8F)) const;
  // !any_greater(abs(1-dot4), 0.01f threshold).
  // This ordered comparison alone can return true for NaN; not a finite check.
  [[nodiscard]] bool is_normalized() const;

  // Scalar component sum, scalar sqrt for length.
  [[nodiscard]] T dot(const Quaternion4& other) const;
  [[nodiscard]] T length_squared() const;
  [[nodiscard]] T length() const;
  // Scalar Vector3 cross/add/scale, not register rotation.
  [[nodiscard]] Vector3<T> rotate(Vector3<T> vector) const;
  [[nodiscard]] Vector3<T> unrotate(Vector3<T> vector) const;
  // Scalar finite test on each component, all builds.
  [[nodiscard]] bool contains_non_finite() const;

protected:
  // Aligned store of register, then diagnostic.
  explicit Quaternion4(Register value);

private:
  // Once-only Debug report that preserves values; compiled out outside Debug.
  // No extra validation/throw/identity repair.
  void diagnostic_check() const;
};

using Quaternion4f = Quaternion4<float>;
using Quaternion4d = Quaternion4<double>;
using Quaternion = Quaternion4d;
// Explicit constant specializations. Declaration only here; definitions belong
// to the implementation stage.
template <> const Quaternion4f Quaternion4f::identity;
template <> const Quaternion4d Quaternion4d::identity;
} // namespace bw::math

#include "bw/math/rotation/quaternion.inl"
#endif // BW_MATH_ROTATION_QUATERNION_HPP
