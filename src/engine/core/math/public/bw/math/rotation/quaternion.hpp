#ifndef BW_MATH_ROTATION_QUATERNION_HPP
#define BW_MATH_ROTATION_QUATERNION_HPP

#include "bw/math/rotation/quaternion_register.hpp"
#include "bw/math/vector/vector3.hpp"

#include <type_traits>

// Read C.49/F.20/GEN.6: explicit member construction, owned return values and
// header-visible algorithms. Source no-init/default and native-store construction
// are intentional exceptions to imposing a uniform initialization algorithm.
namespace bw::math {
// Our typed spelling of the identity branch of source EForceInit (Quat.h:866–869).
struct IdentityInit {};
inline constexpr IdentityInit identity_init{};

template <typename T> struct alignas(16) Quaternion4 {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);
  using Register = std::conditional_t<std::is_same_v<T, float>, simd::Register4f, simd::Register4d>;

  // Unreal f45729c0b6f097038e72b52d366a62baeca3275b, Quat.h:37–68.
  // Owned contiguous XYZW values, align16; ordinary default leaves them uninitialized.
  T x;
  T y;
  T z;
  T w;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
  Quaternion4() = default;

  // Quat.h:866–869: force XYZ zero, W zero or one; no diagnostic.
  explicit Quaternion4(ZeroInit);
  explicit Quaternion4(IdentityInit);
  // Quat.h:90–98,871–880: initialized components followed by diagnostic.
  explicit Quaternion4(T value);
  Quaternion4(T in_x, T in_y, T in_z, T in_w);
  // Quat.h:728–733: cast each component and delegate to component constructor.
  template <typename U>
    requires(!std::is_same_v<T, U>)
  explicit Quaternion4(const Quaternion4<U>& other);

  // Quat.h:110–113: construct through the protected native-register constructor.
  [[nodiscard]] static Quaternion4 from_register(Register value);
  // UnrealMath.cpp:27–28: explicit float/double const identity specializations.
  static const Quaternion4 identity;

  // Quat.h:1025–1048: native product then diagnostic; A*B applies B then A.
  [[nodiscard]] Quaternion4 operator*(const Quaternion4& other) const;
  // Source returns a value copy after mutation, not a reference.
  Quaternion4 operator*=(const Quaternion4& other);

  // Quat.h:385,393,1104–1143: native squared-length >= caller tolerance,
  // compute normalized candidate then select identity. No added diagnostic.
  // UE_SMALL_NUMBER is float even when converted to double. No tolerance guard.
  void normalize(T squared_tolerance = static_cast<T>(1.e-8F));
  [[nodiscard]] Quaternion4 normalized(T squared_tolerance = static_cast<T>(1.e-8F)) const;
  // Quat.h:1146–1155: !any_greater(abs(1-dot4), source 0.01f threshold).
  // This ordered comparison alone can return true for NaN; not a finite check.
  [[nodiscard]] bool is_normalized() const;

  // Quat.h:1097–1101,1159–1169: scalar component sum, scalar sqrt for length.
  [[nodiscard]] T dot(const Quaternion4& other) const;
  [[nodiscard]] T length_squared() const;
  [[nodiscard]] T length() const;
  // Quat.h:1235–1258: scalar Vector3 cross/add/scale, not register rotation.
  [[nodiscard]] Vector3<T> rotate(Vector3<T> vector) const;
  [[nodiscard]] Vector3<T> unrotate(Vector3<T> vector) const;
  // Quat.h:1391–1399: scalar finite test on each component, all builds.
  [[nodiscard]] bool contains_non_finite() const;

  // Value inverse is absent until its source checkSlow dependency is implemented.
  // Raw register inverse is a separately accepted unchecked source operation.
protected:
  // Quat.h:100–106,882–887: aligned store of register, then diagnostic.
  explicit Quaternion4(Register value);

private:
  // Quat.h:588–607; LogMacros.h:400–422: source-default once-only Debug report,
  // preserve values; non-Debug omission. No extra validation/throw/identity repair.
  void diagnostic_check() const;
};

using Quaternion4f = Quaternion4<float>;
using Quaternion4d = Quaternion4<double>;
using Quaternion = Quaternion4d;
// Source Quat.h:768–771 declares explicit constant specializations for Clang.
// Declaration only here; definitions belong to the implementation stage.
template <> const Quaternion4f Quaternion4f::identity;
template <> const Quaternion4d Quaternion4d::identity;
} // namespace bw::math

#include "bw/math/rotation/quaternion.inl"
#endif // BW_MATH_ROTATION_QUATERNION_HPP
