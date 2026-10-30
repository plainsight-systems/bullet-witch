#ifndef BW_PLATFORM_MATH_HPP
#define BW_PLATFORM_MATH_HPP

// Read guidance: F.20 returns scalar values; SIMD.1/3 isolate target selection.
// Unreal pin f45729c0b6f097038e72b52d366a62baeca3275b:
// MacPlatformMath.h:8,27–29 selects ClangPlatformMath on ARM Mac;
// ClangPlatformMath.h:11–19 inherits GenericPlatformMath without overriding these.
namespace bw::platform::math {

// GenericPlatformMath::Sqrt :552–553: precision-specific sqrtf / sqrt.
[[nodiscard]] float square_root(float value);
[[nodiscard]] double square_root(double value);
// GenericPlatformMath::InvSqrt :559–561: precision-specific 1 / sqrt.
[[nodiscard]] float inverse_square_root(float value);
[[nodiscard]] double inverse_square_root(double value);
// GenericPlatformMath::IsFinite :582–590: classify IEEE exponent bits, not
// magnitude or comparison arithmetic (so infinities and NaNs are both false).
[[nodiscard]] bool is_finite(float value);
[[nodiscard]] bool is_finite(double value);

} // namespace bw::platform::math

#if defined(__APPLE__) && defined(__aarch64__)
#include "platform/macos/math.inl"
#else
#error "The selected math implementation is prepared for ARM64 Mac only."
#endif

#endif // BW_PLATFORM_MATH_HPP
