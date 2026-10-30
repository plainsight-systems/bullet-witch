#ifndef BW_PLATFORM_ARM64_REGISTER_COMPOSITION_INL
#define BW_PLATFORM_ARM64_REGISTER_COMPOSITION_INL

// Read Per.11/F.20/GEN.6/SIMD.1: compile-time routing, owned value results and
// header-visible native kernels. The user-selected source algorithm requires
// intrinsics; this is not a measured claim that autovectorization is inadequate.
namespace bw::math::simd {

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:767–794,
// VectorReplicateImpl. Extract the selected lane and duplicate into all lanes.
template <int Lane>
  requires(Lane >= 0 && Lane < 4)
inline Register4f replicate(Register4f value) {
  return vdupq_n_f32(vgetq_lane_f32(value, Lane));
}
template <int Lane>
  requires(Lane >= 0 && Lane < 4)
inline Register4d replicate(Register4d value) {
  // Source double branch chooses the pair at compile time, then duplicates it.
  if constexpr (Lane < 2) {
    const auto pair = vdupq_n_f64(vgetq_lane_f64(value.xy, Lane));
    return {pair, pair};
  } else {
    const auto pair = vdupq_n_f64(vgetq_lane_f64(value.zw, Lane - 2));
    return {pair, pair};
  }
}

namespace detail {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1332–1357,
// VectorSwizzleImpl2. Select the pair combination, then local lane indices.
// Named helper corresponds directly to the source's double-pair helper.
template <int X, int Y> inline float64x2_t swizzle_pair(Register4d value) {
  if constexpr (X < 2) {
    if constexpr (Y < 2) {
      return __builtin_shufflevector(value.xy, value.xy, X, Y);
    } else {
      return __builtin_shufflevector(value.xy, value.zw, X, Y);
    }
  } else {
    if constexpr (Y < 2) {
      return __builtin_shufflevector(value.zw, value.xy, X - 2, Y + 2);
    } else {
      return __builtin_shufflevector(value.zw, value.zw, X - 2, Y);
    }
  }
}
} // namespace detail

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1326–1368,
// VectorSwizzleImpl. Clang permutes lanes directly; doubles assemble two pairs.
template <int X, int Y, int Z, int W>
  requires(X >= 0 && X < 4 && Y >= 0 && Y < 4 && Z >= 0 && Z < 4 && W >= 0 && W < 4)
inline Register4f swizzle(Register4f a) {
  return __builtin_shufflevector(a, a, X, Y, Z, W);
}
template <int X, int Y, int Z, int W>
  requires(X >= 0 && X < 4 && Y >= 0 && Y < 4 && Z >= 0 && Z < 4 && W >= 0 && W < 4)
inline Register4d swizzle(Register4d a) {
  return {detail::swizzle_pair<X, Y>(a), detail::swizzle_pair<Z, W>(a)};
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1465–1480,
// VectorShuffleImpl. Result XY comes from a, ZW from b; each index remains 0–3.
template <int X, int Y, int Z, int W>
  requires(X >= 0 && X < 4 && Y >= 0 && Y < 4 && Z >= 0 && Z < 4 && W >= 0 && W < 4)
inline Register4f shuffle(Register4f a, Register4f b) {
  return __builtin_shufflevector(a, b, X, Y, Z + 4, W + 4);
}
template <int X, int Y, int Z, int W>
  requires(X >= 0 && X < 4 && Y >= 0 && Y < 4 && Z >= 0 && Z < 4 && W >= 0 && W < 4)
inline Register4d shuffle(Register4d a, Register4d b) {
  return {detail::swizzle_pair<X, Y>(a), detail::swizzle_pair<Z, W>(b)};
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:977–995,
// VectorDot3. Float multiplies then clears W and reduces (X+Y)+(Z+0).
inline Register4f dot3(Register4f a, Register4f b) {
  const auto products = vsetq_lane_f32(0.0F, multiply(a, b), 3);
  const auto pairs = vpadd_f32(vget_low_f32(products), vget_high_f32(products));
  return vdupq_lane_f32(vpadd_f32(pairs, pairs), 0);
}
inline Register4d dot3(Register4d a, Register4d b) {
  // Source double uses fused X/Z, then the original Y product; W is not summed.
  const auto xy = vmulq_f64(a.xy, b.xy);
  const auto combined = vfmaq_f64(xy, a.zw, b.zw);
  const auto sum = vadd_f64(vget_low_f64(combined), vget_high_f64(xy));
  const auto pair = vdupq_lane_f64(sum, 0);
  return {pair, pair};
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1022–1039,
// VectorDot4. Float reduces pairwise (X+Y)+(Z+W), broadcasting the result.
inline Register4f dot4(Register4f a, Register4f b) {
  const auto products = multiply(a, b);
  const auto pairs = vpadd_f32(vget_low_f32(products), vget_high_f32(products));
  return vdupq_lane_f32(vpadd_f32(pairs, pairs), 0);
}
inline Register4d dot4(Register4d a, Register4d b) {
  // Source double fuses X/Z and Y/W, then adds the swapped pair; no reassociation.
  const auto xy = vmulq_f64(a.xy, b.xy);
  const auto combined = vfmaq_f64(xy, a.zw, b.zw);
  const auto pair = vaddq_f64(vextq_f64(combined, combined, 1), combined);
  return {pair, pair};
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1574–1587,
// VectorCross. Multiply by cyclic b, fuse subtraction using cyclic a, then
// cyclically reorder the result. W follows arithmetic without a forced clear.
inline Register4f cross(Register4f a, Register4f b) {
  const auto first = multiply(a, swizzle<1, 2, 0, 3>(b));
  const auto difference = negate_multiply_add(swizzle<1, 2, 0, 3>(a), b, first);
  return swizzle<1, 2, 0, 3>(difference);
}
inline Register4d cross(Register4d a, Register4d b) {
  const auto first = multiply(a, swizzle<1, 2, 0, 3>(b));
  const auto difference = negate_multiply_add(swizzle<1, 2, 0, 3>(a), b, first);
  return swizzle<1, 2, 0, 3>(difference);
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:1115–1126,
// VectorCompareGE. Ordered native GE; reinterpret all-one/zero bits, NaN false.
inline Register4f compare_greater_equal(Register4f a, Register4f b) {
  return vreinterpretq_f32_u32(vcgeq_f32(a, b));
}
inline Register4d compare_greater_equal(Register4d a, Register4d b) {
  return {vreinterpretq_f64_u64(vcgeq_f64(a.xy, b.xy)),
          vreinterpretq_f64_u64(vcgeq_f64(a.zw, b.zw))};
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:805–816,
// VectorAbs. Native sign operation, independently on both double pairs.
inline Register4f absolute(Register4f value) { return vabsq_f32(value); }
inline Register4d absolute(Register4d value) { return {vabsq_f64(value.xy), vabsq_f64(value.zw)}; }

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:824–835,
// VectorNegate. Native sign negation, independently on both double pairs.
inline Register4f negate(Register4f value) { return vnegq_f32(value); }
inline Register4d negate(Register4d value) { return {vnegq_f64(value.xy), vnegq_f64(value.zw)}; }

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// UnrealMathVectorCommon.h.inl:102–105, VectorLoadFloat3.
// Read exactly XYZ scalars and construct W0; no fourth scalar access.
inline Register4f load_xyz_zero(const float* values) {
  return {values[0], values[1], values[2], 0.0F};
}
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:518–524,
// VectorLoadFloat3. Load XY pair and single Z, then combine Z with W0.
inline Register4d load_xyz_zero(const double* values) {
  return {vld1q_f64(values), vcombine_f64(vld1_f64(values + 2), vdup_n_f64(0.0))};
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// UnrealMathVectorCommon.h.inl:167–170, VectorLoadFloat3_W1.
// Read exactly XYZ scalars and construct W1; no fourth scalar access.
inline Register4f load_xyz_one(const float* values) {
  return {values[0], values[1], values[2], 1.0F};
}
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:532–537,
// VectorLoadFloat3_W1. Load XY pair and single Z, then combine Z with W1.
inline Register4d load_xyz_one(const double* values) {
  return {vld1q_f64(values), vcombine_f64(vld1_f64(values + 2), vdup_n_f64(1.0))};
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:724–740,
// VectorStoreFloat3. Store XY then Z only; native low-pair extraction replaces
// the source float aliasing cast without changing the three-element extent.
inline void store_xyz(Register4f value, float* destination) {
  vst1_f32(destination, vget_low_f32(value));
  vst1q_lane_f32(destination + 2, value, 2);
}
inline void store_xyz(Register4d value, double* destination) {
  vst1q_f64(destination, value.xy);
  vst1q_lane_f64(destination + 2, value.zw, 0);
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:544–570,1782–1790,
// VectorSet_W0 / VectorSetComponent. Replace W by positive zero, preserve XYZ.
inline Register4f set_w_zero(Register4f value) { return vsetq_lane_f32(0.0F, value, 3); }
inline Register4d set_w_zero(Register4d value) {
  return {value.xy, vsetq_lane_f64(0.0, value.zw, 1)};
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b, UnrealMathNeon.h:544–570,1799–1807,
// VectorSet_W1 / VectorSetComponent. Replace W by one, preserve XYZ.
inline Register4f set_w_one(Register4f value) { return vsetq_lane_f32(1.0F, value, 3); }
inline Register4d set_w_one(Register4d value) {
  return {value.xy, vsetq_lane_f64(1.0, value.zw, 1)};
}

} // namespace bw::math::simd
#endif // BW_PLATFORM_ARM64_REGISTER_COMPOSITION_INL
