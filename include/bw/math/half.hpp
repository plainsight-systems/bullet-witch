// Portable finite half-width conversion used by recovered animation/model
// encodings. Pure value operation; no bytes, ownership, allocation or target
// state. Exponent31 remains finite (65536..131008), preserving the original
// decoder rather than IEEE infinity/NaN. Signed zero and subnormals survive.
// Caller owns format selection and byte bounds. Core I.1 (#ri-explicit) names
// the numerical policy; Performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-
// static-data-into-the-consumer-layout.md) moves conversion to preparation.
// Both design/implementation consultations apply; facial-animation.md records
// source and reference contracts. O(1), no mutable state.
#pragma once
#include <cmath>
#include <cstdint>
namespace bw::math {
inline float finite_half_to_float(std::uint16_t bits) {
  constexpr std::uint32_t kMantissaMask = 0x3ff, kSignMask = 0x8000;
  constexpr unsigned kExponentShift = 10, kExponentMask = 31;
  constexpr float kMantissaScale = 1024;
  constexpr int kSubnormalExponent = -24, kExponentBias = 15;
  const auto exponent = (bits >> kExponentShift) & kExponentMask;
  const auto mantissa = bits & kMantissaMask;
  const float magnitude =
      exponent == 0
          ? std::ldexp(static_cast<float>(mantissa), kSubnormalExponent)
          : std::ldexp(1.0F + static_cast<float>(mantissa) / kMantissaScale,
                       static_cast<int>(exponent) - kExponentBias);
  return (bits & kSignMask) == 0 ? magnitude : -magnitude;
}
}  // namespace bw::math
