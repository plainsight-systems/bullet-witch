#ifndef BW_PLATFORM_ARM64_REGISTER_INL
#define BW_PLATFORM_ARM64_REGISTER_INL
// The tail of bw/math/simd/register.hpp. Opened alone it includes that header,
// which leaves this file out while the guard above is defined.
#include "bw/math/simd/register.hpp"

// Read F.20/I.1 and SIMD.3/GEN.6: register results own values; native intrinsics
// stay in the silicon layer. Header-visible operations have no runtime dispatch.
namespace bw::math::simd {

// Read four lanes in order. Named pair assignment avoids aliasing casts,
// preserving the same native paired load and XY/ZW representation.
inline Register4f load(const float* values) { return vld1q_f32(values); }
inline Register4d load(const double* values) {
  const float64x2x2_t pairs = vld1q_f64_x2(values);
  return {pairs.val[0], pairs.val[1]};
}

// Preserve aligned API contract; NEON selects the ordinary native load.
inline Register4f load_aligned(const float* values) { return load(values); }
inline Register4d load_aligned(const double* values) { return load(values); }

// Write four lanes without retaining memory. Explicit pair construction avoids
// an aliasing cast without changing stored order or instruction choice.
inline void store(Register4f value, float* destination) { vst1q_f32(destination, value); }
inline void store(Register4d value, double* destination) {
  const float64x2x2_t pairs{{value.xy, value.zw}};
  vst1q_f64_x2(destination, pairs);
}

// The same native store serves aligned and ordinary storage.
// This local inline forward shares that operation, without runtime dispatch.
inline void store_aligned(Register4f value, float* destination) { store(value, destination); }
inline void store_aligned(Register4d value, double* destination) { store(value, destination); }

// Duplicate the scalar; double copies the resulting XY pair into ZW.
inline Register4f broadcast(float value) { return vdupq_n_f32(value); }
inline Register4d broadcast(double value) {
  const float64x2_t pair = vdupq_n_f64(value);
  return {pair, pair};
}

// Add corresponding lanes in their own precision.
inline Register4f add(Register4f a, Register4f b) { return vaddq_f32(a, b); }
inline Register4d add(Register4d a, Register4d b) {
  return {vaddq_f64(a.xy, b.xy), vaddq_f64(a.zw, b.zw)};
}

// Subtract b from a per lane.
inline Register4f subtract(Register4f a, Register4f b) { return vsubq_f32(a, b); }
inline Register4d subtract(Register4d a, Register4d b) {
  return {vsubq_f64(a.xy, b.xy), vsubq_f64(a.zw, b.zw)};
}

// Multiply corresponding lanes; paired doubles never narrow.
inline Register4f multiply(Register4f a, Register4f b) { return vmulq_f32(a, b); }
inline Register4d multiply(Register4d a, Register4d b) {
  return {vmulq_f64(a.xy, b.xy), vmulq_f64(a.zw, b.zw)};
}

// Native division per lane; no reciprocal estimate, zero guard or clamp.
inline Register4f divide(Register4f a, Register4f b) { return vdivq_f32(a, b); }
inline Register4d divide(Register4d a, Register4d b) {
  return {vdivq_f64(a.xy, b.xy), vdivq_f64(a.zw, b.zw)};
}

// Fused a*b+accumulator; one rounding, not a rounded intermediate product.
inline Register4f multiply_add(Register4f a, Register4f b, Register4f accumulator) {
  return vfmaq_f32(accumulator, a, b);
}
inline Register4d multiply_add(Register4d a, Register4d b, Register4d accumulator) {
  return {vfmaq_f64(accumulator.xy, a.xy, b.xy), vfmaq_f64(accumulator.zw, a.zw, b.zw)};
}

// Fused accumulator-a*b; preserves the reciprocal refinement rounding.
inline Register4f negate_multiply_add(Register4f a, Register4f b, Register4f accumulator) {
  return vfmsq_f32(accumulator, a, b);
}
inline Register4d negate_multiply_add(Register4d a, Register4d b, Register4d accumulator) {
  return {vfmsq_f64(accumulator.xy, a.xy, b.xy), vfmsq_f64(accumulator.zw, a.zw, b.zw)};
}

// Ordered equality; NaN is false and signed zeros equal.
// Reinterpret all-one/all-zero integer masks as register bits, not numeric casts.
inline Register4f compare_equal(Register4f a, Register4f b) {
  return vreinterpretq_f32_u32(vceqq_f32(a, b));
}
inline Register4d compare_equal(Register4d a, Register4d b) {
  return {vreinterpretq_f64_u64(vceqq_f64(a.xy, b.xy)),
          vreinterpretq_f64_u64(vceqq_f64(a.zw, b.zw))};
}

// Ordered greater-than; NaN is false.
// Reinterpret all-one/all-zero integer masks as register bits, not numeric casts.
inline Register4f compare_greater(Register4f a, Register4f b) {
  return vreinterpretq_f32_u32(vcgtq_f32(a, b));
}
inline Register4d compare_greater(Register4d a, Register4d b) {
  return {vreinterpretq_f64_u64(vcgtq_f64(a.xy, b.xy)),
          vreinterpretq_f64_u64(vcgtq_f64(a.zw, b.zw))};
}

// Select each bit from a for set mask bits, otherwise b; partial masks work.
inline Register4f select(Register4f mask, Register4f a, Register4f b) {
  return vbslq_f32(vreinterpretq_u32_f32(mask), a, b);
}
inline Register4d select(Register4d mask, Register4d a, Register4d b) {
  return {vbslq_f64(vreinterpretq_u64_f64(mask.xy), a.xy, b.xy),
          vbslq_f64(vreinterpretq_u64_f64(mask.zw), a.zw, b.zw)};
}

// Return native precision-specific estimates without refinement.
inline Register4f reciprocal_estimate(Register4f value) { return vrecpeq_f32(value); }
inline Register4d reciprocal_estimate(Register4d value) {
  return {vrecpeq_f64(value.xy), vrecpeq_f64(value.zw)};
}

// Float uses two polynomial Newton refinements, double uses direct division.
inline Register4f reciprocal(Register4f value) {
  // Seed estimate, then fused 2r-value*(r*r).
  Register4f estimate = reciprocal_estimate(value);
  Register4f squared = multiply(estimate, estimate);
  Register4f doubled = add(estimate, estimate);
  estimate = negate_multiply_add(value, squared, doubled);
  // Repeat exactly once; no special-input guard.
  squared = multiply(estimate, estimate);
  doubled = add(estimate, estimate);
  return negate_multiply_add(value, squared, doubled);
}
inline Register4d reciprocal(Register4d value) {
  // Double-one lanes divided by input.
  return divide(broadcast(1.0), value);
}

// Native square root retains signed zero/infinity and invalid-domain NaN.
inline Register4f square_root(Register4f value) { return vsqrtq_f32(value); }
inline Register4d square_root(Register4d value) {
  return {vsqrtq_f64(value.xy), vsqrtq_f64(value.zw)};
}

// Return native estimate, without scalar sqrt or correction.
inline Register4f reciprocal_square_root_estimate(Register4f value) { return vrsqrteq_f32(value); }
inline Register4d reciprocal_square_root_estimate(Register4d value) {
  return {vrsqrteq_f64(value.xy), vrsqrteq_f64(value.zw)};
}

// Estimate then two hardware correction/multiply steps for both precisions.
inline Register4f reciprocal_square_root(Register4f value) {
  // Hardware estimate then first correction.
  Register4f estimate = reciprocal_square_root_estimate(value);
  estimate = multiply(vrsqrtsq_f32(value, multiply(estimate, estimate)), estimate);
  // Repeat correction once, preserving special inputs.
  return multiply(vrsqrtsq_f32(value, multiply(estimate, estimate)), estimate);
}
inline Register4d reciprocal_square_root(Register4d value) {
  // Estimate and correction on each double pair.
  Register4d estimate = reciprocal_square_root_estimate(value);
  Register4d correction{vrsqrtsq_f64(value.xy, vmulq_f64(estimate.xy, estimate.xy)),
                        vrsqrtsq_f64(value.zw, vmulq_f64(estimate.zw, estimate.zw))};
  estimate = multiply(correction, estimate);
  // Second correction; no extra precision pass.
  correction = {vrsqrtsq_f64(value.xy, vmulq_f64(estimate.xy, estimate.xy)),
                vrsqrtsq_f64(value.zw, vmulq_f64(estimate.zw, estimate.zw))};
  return multiply(correction, estimate);
}

// Widen low/high pairs separately; narrow pairs then combine XYZW. Named pair
// extraction avoids pointer aliasing, without numerical change.
inline Register4d widen(Register4f value) {
  return {vcvt_f64_f32(vget_low_f32(value)), vcvt_high_f64_f32(value)};
}
inline Register4f narrow(Register4d value) {
  return vcombine_f32(vcvt_f32_f64(value.xy), vcvt_f32_f64(value.zw));
}

} // namespace bw::math::simd
#endif // BW_PLATFORM_ARM64_REGISTER_INL
