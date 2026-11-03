#ifndef BW_CORE_MATH_REGISTER_HPP
#define BW_CORE_MATH_REGISTER_HPP

#include "bw/platform/math/register_types.hpp"

// Read F.20/I.1: explicit value ownership, precision and alignment contracts.
// Read SIMD.3/GEN.6: silicon kernels remain behind shared names, with header-visible
// implementations. The selected native-intrinsic algorithms take precedence over
// the corpus's portable-library recommendation. No extra library is selected.
namespace bw::math::simd {
using Register4f = platform::Register4f;
using Register4d = platform::Register4d;
using Register = Register4d;
// Four lanes XYZW. Registers own values; default initialization is uninitialized.
// Callers initialize before use. Pointers are borrowed only for the call.
// Four-lane load/store accesses four elements; XYZ operations access exactly three.
// Ordinary load/store requires element alignment; aligned variants require
// 16-byte alignment. No allocation, retaining or runtime dispatch.
// Masks retain floating-register representation, interpreted as bits.
// Operations do not diagnose/clamp NaNs. Hardware FP environment applies.

// Read four element-aligned values in XYZW order; float vld1q, double paired vld1q_x2.
Register4f load(const float* values);
Register4d load(const double* values);

// Aligned API requires 16-byte storage; NEON selects the same load instructions.
Register4f load_aligned(const float* values);
Register4d load_aligned(const double* values);

// Write exactly four lanes to borrowed element-aligned destination; retain nothing.
void store(Register4f value, float* destination);
void store(Register4d value, double* destination);

// Write four lanes to 16-byte-aligned destination; same native stores as unaligned API.
void store_aligned(Register4f value, float* destination);
void store_aligned(Register4d value, double* destination);

// Duplicate scalar in all lanes; doubles duplicate to XY then copy that pair to ZW.
Register4f broadcast(float value);
Register4d broadcast(double value);

// Lane-wise vaddq in selected precision; doubles process each pair.
Register4f add(Register4f a, Register4f b);
Register4d add(Register4d a, Register4d b);

// Lane-wise vsubq: a minus b; doubles process each pair.
Register4f subtract(Register4f a, Register4f b);
Register4d subtract(Register4d a, Register4d b);

// Lane-wise vmulq; double lanes never narrow through float.
Register4f multiply(Register4f a, Register4f b);
Register4d multiply(Register4d a, Register4d b);

// Native vdivq per lane; no zero guard or reciprocal-estimate substitution.
Register4f divide(Register4f a, Register4f b);
Register4d divide(Register4d a, Register4d b);

// Native vfmaq: a*b+accumulator with one fused rounding, not separate multiply/add.
Register4f multiply_add(Register4f a, Register4f b, Register4f accumulator);
Register4d multiply_add(Register4d a, Register4d b, Register4d accumulator);

// Native vfmsq: accumulator-a*b fused; needed by float reciprocal refinement.
Register4f negate_multiply_add(Register4f a, Register4f b, Register4f accumulator);
Register4d negate_multiply_add(Register4d a, Register4d b, Register4d accumulator);

// Ordered equality: all bits one for true lane, zero otherwise; NaN compares false.
Register4f compare_equal(Register4f a, Register4f b);
Register4d compare_equal(Register4d a, Register4d b);

// Ordered greater-than: all bits one for true lane, zero otherwise; NaN false.
Register4f compare_greater(Register4f a, Register4f b);
Register4d compare_greater(Register4d a, Register4d b);

// Bit-select each bit from a when mask bit is one, otherwise b; not Boolean numeric
// conversion.
Register4f select(Register4f mask, Register4f a, Register4f b);
Register4d select(Register4d mask, Register4d a, Register4d b);

// Native vrecpeq estimate for each float lane or double pair; no refinement.
Register4f reciprocal_estimate(Register4f value);
Register4d reciprocal_estimate(Register4d value);

// Float: estimate then twice square r, double r, fused 2r-value*r*r. Double: native
// 1/value
// division.
Register4f reciprocal(Register4f value);
Register4d reciprocal(Register4d value);

// Native vsqrtq per lane; preserve signed zero, infinity and invalid-domain NaN.
Register4f square_root(Register4f value);
Register4d square_root(Register4d value);

// Native vrsqrteq estimate, precision-specific; no scalar sqrt or corrective branch.
Register4f reciprocal_square_root_estimate(Register4f value);
Register4d reciprocal_square_root_estimate(Register4d value);

// Estimate then two refinements: square estimate, vrsqrtsq(value,square),
// multiply estimate. Double repeats on each pair.
Register4f reciprocal_square_root(Register4f value);
Register4d reciprocal_square_root(Register4d value);

// Widen float lanes to paired doubles exactly; narrow paired doubles with native
// conversion and recombine XYZW. Named conversion spelling, no hidden narrowing.
Register4d widen(Register4f value);
Register4f narrow(Register4d value);

// Register composition tier. Read I.9/F.20/GEN.6: compile-time lane domain,
// owned register results and header-visible target operations. No runtime fallback.

// dot3: Float: multiply lanes, set W0, pairwise add and broadcast. Double: XY
// products, fused ZW products; add combined X/Z to original Y, broadcast.
Register4f dot3(Register4f a, Register4f b);
Register4d dot3(Register4d a, Register4d b);

// dot4: Float: pairwise (X+Y)+(Z+W). Double: XY products fused with ZW, add
// swapped pair; broadcast. Preserve grouping and fused rounding.
Register4f dot4(Register4f a, Register4f b);
Register4d dot4(Register4d a, Register4d b);

// cross: multiply a by cyclic-swizzled b, fused subtract cyclic-swizzled a times b,
// then cyclic-swizzle result. W follows arithmetic; no forced zero or guard.
Register4f cross(Register4f a, Register4f b);
Register4d cross(Register4d a, Register4d b);

// compare_greater_equal: ordered native GE compare, reinterpret all-one/zero mask
// bits; unordered NaN false.
Register4f compare_greater_equal(Register4f a, Register4f b);
Register4d compare_greater_equal(Register4d a, Register4d b);

// absolute: native absolute value sign operation on float or each double pair.
Register4f absolute(Register4f value);
Register4d absolute(Register4d value);

// negate: native negation sign operation on float or each double pair.
Register4f negate(Register4f value);
Register4d negate(Register4d value);

// XYZ loads borrow three initialized, element-aligned scalars for the call.
// load_xyz_zero: Double: load XY pair and Z scalar then combine with W0. Float
// reads exactly three scalars and constructs W0; do not read a fourth scalar.
Register4f load_xyz_zero(const float* values);
Register4d load_xyz_zero(const double* values);

// load_xyz_one: Double: load XY pair and Z scalar then combine with W1. Float
// reads exactly three scalars and constructs W1; do not read a fourth scalar.
Register4f load_xyz_one(const float* values);
Register4d load_xyz_one(const double* values);

// store_xyz: borrow three writable, element-aligned scalars for the call. Store
// XY pair then Z lane. Exactly three writes; W is not written.
void store_xyz(Register4f value, float* destination);
void store_xyz(Register4d value, double* destination);

// set_w_zero: replace lane 3 by positive zero; preserve XYZ bits. Double retains
// XY and replaces high lane of ZW.
Register4f set_w_zero(Register4f value);
Register4d set_w_zero(Register4d value);

// set_w_one: replace lane 3 by one; preserve XYZ bits. Double retains XY and
// replaces high lane of ZW.
Register4f set_w_one(Register4f value);
Register4d set_w_one(Register4d value);

// replicate: extract chosen lane and duplicate. Double selects XY for indices 0/1
// or ZW for 2/3, then duplicates resulting pair.
template <int Lane>
  requires(Lane >= 0 && Lane < 4)
Register4f replicate(Register4f value);
template <int Lane>
  requires(Lane >= 0 && Lane < 4)
Register4d replicate(Register4d value);

// swizzle: Clang float builtin shuffle. Double selects each output pair through
// XY/XY, XY/ZW, ZW/XY or ZW/ZW compile-time branches; adjust local lane indices.
template <int X, int Y, int Z, int W>
  requires(X >= 0 && X < 4 && Y >= 0 && Y < 4 && Z >= 0 && Z < 4 && W >= 0 && W < 4)
Register4f swizzle(Register4f a);
template <int X, int Y, int Z, int W>
  requires(X >= 0 && X < 4 && Y >= 0 && Y < 4 && Z >= 0 && Z < 4 && W >= 0 && W < 4)
Register4d swizzle(Register4d a);

// shuffle: Clang float builtin uses X,Y from a and Z+4,W+4 from b. Double
// obtains XY from a via pair helper and ZW from b; indices remain 0–3.
template <int X, int Y, int Z, int W>
  requires(X >= 0 && X < 4 && Y >= 0 && Y < 4 && Z >= 0 && Z < 4 && W >= 0 && W < 4)
Register4f shuffle(Register4f a, Register4f b);
template <int X, int Y, int Z, int W>
  requires(X >= 0 && X < 4 && Y >= 0 && Y < 4 && Z >= 0 && Z < 4 && W >= 0 && W < 4)
Register4d shuffle(Register4d a, Register4d b);

// any_greater: ordered greater masks reduced across every lane.
// bool spells the nonzero/zero mask meaning; NaN comparisons contribute false.
// Double checks both XY and ZW. No scalar extraction loop or runtime dispatch.
bool any_greater(Register4f a, Register4f b);
bool any_greater(Register4d a, Register4d b);

} // namespace bw::math::simd
#include "bw/platform/math/register.inl"
#endif // BW_CORE_MATH_REGISTER_HPP
