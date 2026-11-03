#ifndef BW_MATH_ROTATION_QUATERNION_REGISTER_HPP
#define BW_MATH_ROTATION_QUATERNION_REGISTER_HPP

#include "bw/math/simd/register.hpp"

// Read F.8/F.20: algebra consumes owned register values and returns owned values.
// Read GEN.6: definitions remain header-visible; no runtime kernel routing.
namespace bw::math::simd {
// Normalize all four lanes when dot4 >= 1.e-8f (float) or 1.e-8 (double).
// Otherwise preserve fallback bits. The normalized candidate is still evaluated.
// No early-out, finite-value repair or promise to suppress FP exceptions.
Register4f normalize_safe(Register4f value, Register4f fallback);
Register4d normalize_safe(Register4d value, Register4d fallback);

// Safe normalization with identity XYZW (0,0,0,1); no hemisphere selection.
Register4f normalize_quaternion(Register4f value);
Register4d normalize_quaternion(Register4d value);

// Hamilton product A*B applies B then A. Preserve the X, Y, Z accumulation order;
// no implicit normalization. Target-selected implementation owns the kernel.
Register4f quaternion_multiply(Register4f a, Register4f b);
Register4d quaternion_multiply(Register4d a, Register4d b);

// Unit quaternion inverse: numeric multiply by (-1,-1,-1,+1).
// Assumes unit input; does not divide by length or validate/normalize input.
Register4f quaternion_inverse(Register4f unit_quaternion);
Register4d quaternion_inverse(Register4d unit_quaternion);

// Unit quaternion and vector W=0 are preconditions, not checked here.
// Two cross products and a fused multiply-add rotate XYZ; no scalar fallback.
Register4f quaternion_rotate(Register4f unit_quaternion, Register4f vector_w0);
Register4d quaternion_rotate(Register4d unit_quaternion, Register4d vector_w0);

// Unit inverse followed by rotation; same unit/W0 preconditions.
Register4f quaternion_inverse_rotate(Register4f unit_quaternion, Register4f vector_w0);
Register4d quaternion_inverse_rotate(Register4d unit_quaternion, Register4d vector_w0);
} // namespace bw::math::simd

#include "bw/math/rotation/quaternion_register.inl"
#include "bw/platform/math/quaternion_register.inl"

#endif // BW_MATH_ROTATION_QUATERNION_REGISTER_HPP
