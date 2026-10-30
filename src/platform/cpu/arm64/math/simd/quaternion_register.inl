#ifndef BW_PLATFORM_ARM64_QUATERNION_REGISTER_INL
#define BW_PLATFORM_ARM64_QUATERNION_REGISTER_INL

#include <array>

// Read F.8/F.20/GEN.6: value-returning inline target kernels preserve source
// arithmetic without dispatch, allocation or mutable initialization state.
namespace bw::math::simd {
// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// UnrealMathNeon.h:2318–2326, VectorQuaternionMultiply2(float).
inline Register4f quaternion_multiply(Register4f a, Register4f b) {
  // Source constants:86–88 represented by immutable typed arrays, loaded through
  // the accepted native primitive. Product A*B applies B then A.
  static constexpr std::array<float, 4> x_signs{1.F, -1.F, 1.F, -1.F};
  static constexpr std::array<float, 4> y_signs{1.F, 1.F, -1.F, -1.F};
  static constexpr std::array<float, 4> z_signs{-1.F, 1.F, 1.F, -1.F};
  auto result = multiply(replicate<3>(a), b);
  // Keep each term multiplication separate before its fused sign accumulation;
  // accumulate X, Y then Z in source order, without implicit normalization.
  const auto x_term = multiply(replicate<0>(a), swizzle<3, 2, 1, 0>(b));
  result = multiply_add(x_term, load(x_signs.data()), result);
  const auto y_term = multiply(replicate<1>(a), swizzle<2, 3, 0, 1>(b));
  result = multiply_add(y_term, load(y_signs.data()), result);
  const auto z_term = multiply(replicate<2>(a), swizzle<1, 0, 3, 2>(b));
  return multiply_add(z_term, load(z_signs.data()), result);
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b,
// UnrealMathNeon.h:2328–2336, VectorQuaternionMultiply2(double).
inline Register4d quaternion_multiply(Register4d a, Register4d b) {
  // Source constants:89–91 stay double; accepted primitives retain XY/ZW pairs.
  static constexpr std::array<double, 4> x_signs{1., -1., 1., -1.};
  static constexpr std::array<double, 4> y_signs{1., 1., -1., -1.};
  static constexpr std::array<double, 4> z_signs{-1., 1., 1., -1.};
  auto result = multiply(replicate<3>(a), b);
  // Rounded lane products precede fused sign accumulation, X then Y then Z.
  const auto x_term = multiply(replicate<0>(a), swizzle<3, 2, 1, 0>(b));
  result = multiply_add(x_term, load(x_signs.data()), result);
  const auto y_term = multiply(replicate<1>(a), swizzle<2, 3, 0, 1>(b));
  result = multiply_add(y_term, load(y_signs.data()), result);
  const auto z_term = multiply(replicate<2>(a), swizzle<1, 0, 3, 2>(b));
  return multiply_add(z_term, load(z_signs.data()), result);
}
} // namespace bw::math::simd
#endif // BW_PLATFORM_ARM64_QUATERNION_REGISTER_INL
