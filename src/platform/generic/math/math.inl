#ifndef BW_PLATFORM_GENERIC_MATH_INL
#define BW_PLATFORM_GENERIC_MATH_INL
// The tail of bw/platform/math/math.hpp. Opened alone it includes that header,
// which leaves this file out while the guard above is defined.
#include "bw/platform/math/math.hpp"

// Read F.20/ES.23 and SIMD.1/3: return values, exact precision, no runtime dispatch.

#include <bit>
#include <cmath>
#include <cstdint>

namespace bw::platform::math {

// square_root(float) and square_root(double)
// Call the precision-matched native sqrtf or sqrt. Preserve their native
// arithmetic domain behavior; do not add clamping or exceptions.
inline float square_root(float value) { return std::sqrt(value); }
inline double square_root(double value) { return std::sqrt(value); }

// inverse_square_root(float) and inverse_square_root(double)
// Divide a precision-matched one by precision-matched square root. This generic
// scalar path is selected on ARM Mac; it is not the NEON register rsqrt path.
inline float inverse_square_root(float value) { return 1.0F / std::sqrt(value); }
inline double inverse_square_root(double value) { return 1.0 / std::sqrt(value); }

// sine, cosine, tangent and arc_tangent
// Call the precision-matched native function, radians; native domain
// behavior, no clamping.
inline float sine(float radians) { return std::sin(radians); }
inline double sine(double radians) { return std::sin(radians); }
inline float cosine(float radians) { return std::cos(radians); }
inline double cosine(double radians) { return std::cos(radians); }
inline float tangent(float radians) { return std::tan(radians); }
inline double tangent(double radians) { return std::tan(radians); }
inline float arc_tangent(float value) { return std::atan(value); }
inline double arc_tangent(double value) { return std::atan(value); }

// is_finite(float) and is_finite(double)
// Bit-cast float/double to matching unsigned integer width; mask exponent bits;
// return false when every exponent bit is one. NaNs and infinities are false,
// while zeros, subnormals and maximum finite values are true.
inline bool is_finite(float value) {
  return (std::bit_cast<std::uint32_t>(value) & 0x7F800000U) != 0x7F800000U;
}
inline bool is_finite(double value) {
  return (std::bit_cast<std::uint64_t>(value) & 0x7FF0000000000000ULL) != 0x7FF0000000000000ULL;
}

} // namespace bw::platform::math
#endif // BW_PLATFORM_GENERIC_MATH_INL
