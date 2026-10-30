#ifndef BW_MATH_ROTATION_QUATERNION_REGISTER_INL
#define BW_MATH_ROTATION_QUATERNION_REGISTER_INL

#include <array>

// Read F.8/F.20/GEN.6: return owned values; keep source composites visible to the
// inliner without mutable state, dispatch or a blanket force-inline attribute.
namespace bw::math::simd {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// UnrealMathVectorCommon.h.inl:425–432, VectorNormalizeSafe(float).
// Source API intentionally orders same-register operands by role.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline Register4f normalize_safe(Register4f value, Register4f fallback) {
  // Retain native dot grouping and the float literal from constants:46.
  const auto squared_length = dot4(value, value);
  const auto usable = compare_greater_equal(squared_length, broadcast(1.e-8F));
  // Common:374–378 aliases accurate rsqrt to the accepted refined operation.
  // Evaluate the candidate even for an unusable length, then select fallback bits.
  const auto inverse_length = reciprocal_square_root(squared_length);
  const auto normalized = multiply(inverse_length, value);
  return select(usable, normalized, fallback);
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// UnrealMathVectorCommon.h.inl:434–441, VectorNormalizeSafe(double).
// Source API intentionally orders same-register operands by role.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline Register4d normalize_safe(Register4d value, Register4d fallback) {
  // Constants:64 is double 1.e-8, not the promoted float threshold.
  const auto squared_length = dot4(value, value);
  const auto usable = compare_greater_equal(squared_length, broadcast(1.e-8));
  // Common:380–383 selects the same two-refinement double rsqrt. No early-out.
  const auto inverse_length = reciprocal_square_root(squared_length);
  const auto normalized = multiply(inverse_length, value);
  return select(usable, normalized, fallback);
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// Common:599–603, VectorNormalizeQuaternion(float), constants:43.
// Local typed constant represents the source identity without dynamic init.
inline Register4f normalize_quaternion(Register4f value) {
  static constexpr std::array<float, 4> identity{0.F, 0.F, 0.F, 1.F};
  return normalize_safe(value, load(identity.data()));
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// Common:605–608, VectorNormalizeQuaternion(double), constants:61.
// Safe normalization supplies identity; no sign-hemisphere adjustment.
inline Register4d normalize_quaternion(Register4d value) {
  static constexpr std::array<double, 4> identity{0., 0., 0., 1.};
  return normalize_safe(value, load(identity.data()));
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// Common:719–727, VectorQuaternionInverse(float), constants:83.
// Unit inverse is numeric sign multiplication, with no length division or guard.
inline Register4f quaternion_inverse(Register4f unit_quaternion) {
  static constexpr std::array<float, 4> signs{-1.F, -1.F, -1.F, 1.F};
  return multiply(load(signs.data()), unit_quaternion);
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// Common:719–727, VectorQuaternionInverse(double), constants:84.
// Keep sign multiplication in double; this is not a general non-unit inverse.
inline Register4d quaternion_inverse(Register4d unit_quaternion) {
  static constexpr std::array<double, 4> signs{-1., -1., -1., 1.};
  return multiply(load(signs.data()), unit_quaternion);
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// Common:741–761, VectorQuaternionRotateVector(float).
inline Register4f quaternion_rotate(Register4f unit_quaternion, Register4f vector_w0) {
  // Build T=2 cross(Q,V); source assumes unit Q and W0 vector without guards.
  const auto scalar = replicate<3>(unit_quaternion);
  const auto crossed = cross(unit_quaternion, vector_w0);
  const auto twice_crossed = add(crossed, crossed);
  // Fused W*T+V, then cross(Q,T) and add. Preserve source W arithmetic.
  const auto first = multiply_add(scalar, twice_crossed, vector_w0);
  const auto second = cross(unit_quaternion, twice_crossed);
  return add(first, second);
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// Common:764–773, VectorQuaternionRotateVector(double).
inline Register4d quaternion_rotate(Register4d unit_quaternion, Register4d vector_w0) {
  // Same composite with native double pairs, retaining both cross/FMA orders.
  const auto scalar = replicate<3>(unit_quaternion);
  const auto crossed = cross(unit_quaternion, vector_w0);
  const auto twice_crossed = add(crossed, crossed);
  const auto first = multiply_add(scalar, twice_crossed, vector_w0);
  const auto second = cross(unit_quaternion, twice_crossed);
  return add(first, second);
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// Common:783–791, VectorQuaternionInverseRotateVector(float).
// Invert the unit quaternion, then reuse its source forward rotation composite.
// Source API intentionally orders same-register operands by role.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline Register4f quaternion_inverse_rotate(Register4f unit_quaternion, Register4f vector_w0) {
  const auto inverse = quaternion_inverse(unit_quaternion);
  return quaternion_rotate(inverse, vector_w0);
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// Common:793–797, VectorQuaternionInverseRotateVector(double).
// No extra normalization or scalar path between unit inversion and rotation.
// Source API intentionally orders same-register operands by role.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
inline Register4d quaternion_inverse_rotate(Register4d unit_quaternion, Register4d vector_w0) {
  const auto inverse = quaternion_inverse(unit_quaternion);
  return quaternion_rotate(inverse, vector_w0);
}
} // namespace bw::math::simd
#endif // BW_MATH_ROTATION_QUATERNION_REGISTER_INL
