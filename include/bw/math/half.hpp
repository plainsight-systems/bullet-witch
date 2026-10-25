// Portable finite half-width conversion used by recovered animation/model
// encodings. Pure value operation; no bytes, ownership, allocation or target
// state. Texture output also exposes the original scalar/pair packing policies;
// Core SL.con.3 (#rsl-bounds) bounds shifts and Performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) keeps
// this value conversion allocation-free (both phases retrieved). Exponent31
// remains finite (65536..131008), preserving the original decoder rather than
// IEEE infinity/NaN. Signed zero and subnormals survive. Caller owns format
// selection and byte bounds. Core I.1 (#ri-explicit) names the numerical
// policy; Performance CDSA.32 (guidelines/cpu-dsa/CDSA.32-transform-
// static-data-into-the-consumer-layout.md) moves conversion to preparation.
// Both design/implementation consultations apply; facial-animation.md records
// source and reference contracts. O(1), no mutable state.
#pragma once
#include <algorithm>
#include <bit>
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
enum class FiniteHalfEncoding : std::uint8_t { scalar, packed_pair };
// Inverse finite encoding, with an explicit original packing policy. Saturate
// magnitudes beyond the largest finite code; preserve the sign of zero.
// NaN maps to the largest magnitude code. The original texture staging
// conversion uses this policy (texture-mip-filter.md); this is not IEEE HALF.
inline std::uint16_t float_to_finite_half(
    float value, FiniteHalfEncoding encoding = FiniteHalfEncoding::scalar) {
  constexpr unsigned kSignShift = 16, kFloatExponentShift = 23;
  constexpr std::uint32_t kFloatFractionMask = 0x7fffff;
  constexpr std::uint32_t kFloatHiddenBit = 0x800000;
  constexpr std::uint32_t kHalfSign = 0x8000, kHalfMaximum = 0x7fff;
  constexpr int kExponentAdjustment = 112;
  constexpr int kMaximumExponent = 31, kMinimumRoundedExponent = -10;
  constexpr unsigned kNormalDiscard = 13;
  const auto bits = std::bit_cast<std::uint32_t>(value);
  const auto sign = (bits >> kSignShift) & kHalfSign;
  // The original pair converter uses a float add to normalize subnormals,
  // integer halfway-down rounding, and saturation at code 0x7c00. Preserve
  // that distinct policy; the scalar tail retains the full finite range.
  if (encoding == FiniteHalfEncoding::packed_pair) {
    constexpr std::uint32_t kMagnitudeMask = 0x7fffffff;
    constexpr std::uint32_t kMinimumNormalBits = 0x38800000;
    constexpr std::uint32_t kPackedBias = 0xc8000fff;
    constexpr std::uint32_t kPackedMaximum = 0x7c00;
    auto magnitude = bits & kMagnitudeMask;
    const auto absolute = std::bit_cast<float>(magnitude);
    const auto minimum = std::bit_cast<float>(kMinimumNormalBits);
    if (absolute < minimum) {
      magnitude =
          std::bit_cast<std::uint32_t>(absolute + minimum) - kFloatHiddenBit;
    }
    const auto shifted = (magnitude + kPackedBias) >> kNormalDiscard;
    return static_cast<std::uint16_t>(sign | std::min(shifted, kPackedMaximum));
  }
  const int exponent = static_cast<int>((bits >> kFloatExponentShift) & 255U) -
                       kExponentAdjustment;
  if (exponent > kMaximumExponent) {
    return static_cast<std::uint16_t>(sign | kHalfMaximum);
  }
  if (exponent < kMinimumRoundedExponent) {
    return static_cast<std::uint16_t>(sign);
  }
  const auto fraction = bits & kFloatFractionMask;
  const auto significand =
      exponent <= 0 ? fraction | kFloatHiddenBit : fraction;
  const auto discard =
      exponent <= 0 ? static_cast<unsigned>(14 - exponent) : kNormalDiscard;
  const auto retained = significand >> discard;
  const auto remainder = significand & ((1U << discard) - 1U);
  const auto halfway = 1U << (discard - 1U);
  const auto rounded = retained + static_cast<unsigned>(remainder > halfway ||
                                                        (remainder == halfway &&
                                                         (retained & 1U) != 0));
  const auto magnitude =
      exponent <= 0 ? rounded
                    : (static_cast<std::uint32_t>(exponent) << 10U) + rounded;
  return static_cast<std::uint16_t>(sign | std::min(magnitude, kHalfMaximum));
}
}  // namespace bw::math
