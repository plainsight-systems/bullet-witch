#ifndef BW_CORE_MATH_REGISTER_HPP
#define BW_CORE_MATH_REGISTER_HPP

#include "platform/register_types.hpp"

// Read F.20/I.1: explicit value ownership, precision and alignment contracts.
// Read SIMD.3/GEN.6: silicon kernels remain behind shared names, with header-visible
// implementations. User-selected Unreal algorithms take precedence over
// the corpus's portable-library recommendation. No extra library is selected.
namespace bw::math::simd {
using Register4f = platform::Register4f;
using Register4d = platform::Register4d;
using Register = Register4d;
// Four lanes XYZW. Registers own values; default initialization is uninitialized.
// Callers initialize before use. Pointers are borrowed only for the call, valid
// for four elements. Ordinary load/store requires element alignment, aligned
// variants require 16-byte alignment. No allocation, retaining or runtime dispatch.
// Masks retain source floating-register representation, interpreted as bits.
// Source operations do not diagnose/clamp NaNs. Hardware FP environment applies.

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:478–489, VectorLoad.
// Read four element-aligned values in XYZW order; float vld1q, double paired vld1q_x2.
Register4f load(const float* values);
Register4d load(const double* values);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:581–589, VectorLoadAligned.
// Aligned API requires 16-byte storage; NEON selects the same load instructions.
Register4f load_aligned(const float* values);
Register4d load_aligned(const double* values);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:697–706, VectorStore.
// Write exactly four lanes to borrowed element-aligned destination; retain nothing.
void store(Register4f value, float* destination);
void store(Register4d value, double* destination);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:663–672, VectorStoreAligned.
// Write four lanes to 16-byte-aligned destination; same native stores as unaligned API.
void store_aligned(Register4f value, float* destination);
void store_aligned(Register4d value, double* destination);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:644–655, VectorSetFloat1.
// Duplicate scalar in all lanes; doubles duplicate to XY then copy that pair to ZW.
Register4f broadcast(float value);
Register4d broadcast(double value);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:844–856, VectorAdd.
// Lane-wise vaddq in selected precision; doubles process each pair.
Register4f add(Register4f a, Register4f b);
Register4d add(Register4d a, Register4d b);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:865–877, VectorSubtract.
// Lane-wise vsubq: a minus b; doubles process each pair.
Register4f subtract(Register4f a, Register4f b);
Register4d subtract(Register4d a, Register4d b);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:886–903, VectorMultiply.
// Lane-wise vmulq; double lanes never narrow through float.
Register4f multiply(Register4f a, Register4f b);
Register4d multiply(Register4d a, Register4d b);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:912–924, VectorDivide.
// Native vdivq per lane; no zero guard or reciprocal-estimate substitution.
Register4f divide(Register4f a, Register4f b);
Register4d divide(Register4d a, Register4d b);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:934–946, VectorMultiplyAdd.
// Native vfmaq: a*b+accumulator with one fused rounding, not separate multiply/add.
Register4f multiply_add(Register4f a, Register4f b, Register4f accumulator);
Register4d multiply_add(Register4d a, Register4d b, Register4d accumulator);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:955–966,
// VectorNegateMultiplyAdd. Native vfmsq: accumulator-a*b fused; needed by float reciprocal
// refinement.
Register4f negate_multiply_add(Register4f a, Register4f b, Register4f accumulator);
Register4d negate_multiply_add(Register4d a, Register4d b, Register4d accumulator);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1050–1063, VectorCompareEQ.
// Ordered equality: all bits one for true lane, zero otherwise; NaN compares false.
Register4f compare_equal(Register4f a, Register4f b);
Register4d compare_equal(Register4d a, Register4d b);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1094–1105, VectorCompareGT.
// Ordered greater-than: all bits one for true lane, zero otherwise; NaN false.
Register4f compare_greater(Register4f a, Register4f b);
Register4d compare_greater(Register4d a, Register4d b);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1178–1189, VectorSelect.
// Bit-select each bit from a when mask bit is one, otherwise b; not Boolean numeric conversion.
Register4f select(Register4f mask, Register4f a, Register4f b);
Register4d select(Register4d mask, Register4d a, Register4d b);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1628–1640,
// VectorReciprocalEstimate. Native vrecpeq estimate for each float lane or double pair; no
// refinement.
Register4f reciprocal_estimate(Register4f value);
Register4d reciprocal_estimate(Register4d value);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1648–1670, VectorReciprocal.
// Float: estimate then twice square r, double r, fused 2r-value*r*r. Double: native 1/value
// division.
Register4f reciprocal(Register4f value);
Register4d reciprocal(Register4d value);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1680–1690, VectorSqrt.
// Native vsqrtq per lane; preserve signed zero, infinity and invalid-domain NaN.
Register4f square_root(Register4f value);
Register4d square_root(Register4d value);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1698–1710,
// VectorReciprocalSqrtEstimate. Native vrsqrteq estimate, precision-specific; no scalar sqrt or
// corrective branch.
Register4f reciprocal_square_root_estimate(Register4f value);
Register4d reciprocal_square_root_estimate(Register4d value);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1717–1741,
// VectorReciprocalSqrt. Estimate then two refinements: square estimate, vrsqrtsq(value,square),
// multiply estimate. Double repeats on each pair.
Register4f reciprocal_square_root(Register4f value);
Register4d reciprocal_square_root(Register4d value);

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:154–158,404–413.
// Widen float lanes to paired doubles exactly; narrow paired doubles with native
// conversion and recombine XYZW. Named conversion spelling, no hidden narrowing.
Register4d widen(Register4f value);
Register4f narrow(Register4d value);

} // namespace bw::math::simd
#include "platform/register.inl"
#endif // BW_CORE_MATH_REGISTER_HPP
