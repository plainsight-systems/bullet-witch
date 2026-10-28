// Source curve and shared effect-RNG execution; see particle_sampling.hpp for
// ownership, ordering, allocation and rejection contracts. The unusual keyed
// terminal result and two-draw minimum guard are intentional original behavior.
// Core SL.con.3 (#rsl-bounds) protects public input; I.11 (#ri-raw) borrows
// decoded values. Performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps hot sampling allocation-free;
// CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) avoids reparsing.
#include "bw/effects/particle_sampling.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace bw::effects {
namespace {
constexpr float kInverseRandomMaximum = 0.000030518509447574615F;
constexpr float kInverseLastSample = 0.032258063554763794F;
constexpr std::size_t kLastSample = formats::kEffectCurveSamples - 1;
constexpr std::int32_t kMaximumKeys = 16;
constexpr std::uint32_t kMultiplier = 0x41c64e6d, kIncrement = 0x3039;
constexpr std::uint32_t kRandomMask = 0x7fff, kRandomShift = 16;
}  // namespace

float sample_scalar_curve(const formats::EffectScalarCurve& curve,
                          std::uint32_t sample_index, float fraction) {
  if (!std::isfinite(fraction) || fraction < 0 || fraction > 1 ||
      curve.mode > kMaximumKeys) {
    throw std::invalid_argument("Invalid particle curve sample");
  }
  if (sample_index >= kLastSample) return curve.values.back();
  if (curve.mode <= 0) {
    return (1 - fraction) * curve.values.at(sample_index) +
           curve.values.at(sample_index + 1) * fraction;
  }
  const auto normalized =
      (static_cast<float>(sample_index) + fraction) * kInverseLastSample;
  for (std::int32_t key = 1; key < curve.mode; ++key) {
    const auto index = static_cast<std::size_t>(key) * 2;
    const auto end = curve.values.at(index);
    if (normalized <= end) {
      const auto width = end - curve.values.at(index - 2);
      if (!(width > 0)) {
        throw std::invalid_argument("Invalid particle curve key interval");
      }
      const auto previous_weight = (end - normalized) / width;
      return (1 - previous_weight) * curve.values.at(index + 1) +
             curve.values.at(index - 1) * previous_weight;
    }
  }
  // Source indexes float word mode (including the preceding mode word).
  return curve.values.at(static_cast<std::size_t>(curve.mode - 1));
}

std::uint16_t ParticleRandom::next() noexcept {
  state_ = state_ * kMultiplier + kIncrement;
  return static_cast<std::uint16_t>((state_ >> kRandomShift) & kRandomMask);
}
float ParticleRandom::between(float lower, float upper) {
  if (!std::isfinite(lower) || !std::isfinite(upper) ||
      !std::isfinite(upper - lower)) {
    throw std::invalid_argument("Invalid particle random bounds");
  }
  return lower +
         static_cast<float>(next()) * (upper - lower) * kInverseRandomMaximum;
}
float ParticleRandom::guarded_jitter(float center, float radius,
                                     float minimum) {
  if (!std::isfinite(center) || !std::isfinite(radius) ||
      !std::isfinite(minimum) || !std::isfinite(radius + radius) ||
      !std::isfinite(center + radius) || !std::isfinite(center - radius)) {
    throw std::invalid_argument("Invalid particle jitter bounds");
  }
  if (center + between(-radius, radius) < minimum) return minimum;
  return center + between(-radius, radius);
}
std::int32_t ParticleRandom::count(std::int32_t center, std::int32_t radius) {
  const auto lower = static_cast<std::int64_t>(center) - radius;
  const auto upper = static_cast<std::int64_t>(center) + radius;
  if (radius < 0 || lower < std::numeric_limits<std::int32_t>::min() ||
      upper > std::numeric_limits<std::int32_t>::max()) {
    throw std::invalid_argument("Invalid particle random count bounds");
  }
  const auto width = upper - lower + 1;
  return static_cast<std::int32_t>(lower + next() % width);
}
}  // namespace bw::effects
