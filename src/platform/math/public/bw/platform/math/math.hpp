#ifndef BW_PLATFORM_MATH_HPP
#define BW_PLATFORM_MATH_HPP

// Read guidance: F.20 returns scalar values; SIMD.1/3 isolate target selection.
// ARM Mac selects the generic scalar implementations of these functions.
namespace bw::platform::math {

// Precision-specific sqrtf / sqrt.
[[nodiscard]] float square_root(float value);
[[nodiscard]] double square_root(double value);
// Precision-specific 1 / sqrt.
[[nodiscard]] float inverse_square_root(float value);
[[nodiscard]] double inverse_square_root(double value);
// Classify IEEE exponent bits, not
// magnitude or comparison arithmetic (so infinities and NaNs are both false).
[[nodiscard]] bool is_finite(float value);
[[nodiscard]] bool is_finite(double value);

} // namespace bw::platform::math

#if defined(__APPLE__) && defined(__aarch64__)
#include "../../../../../os/macos/math/math.inl"
#else
#error "The selected math implementation is prepared for ARM64 Mac only."
#endif

#endif // BW_PLATFORM_MATH_HPP
