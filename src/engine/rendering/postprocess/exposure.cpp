#include "bw/rendering/postprocess/exposure.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

// Responsibility: the out-of-line pure functions of
// bw/rendering/postprocess/exposure.hpp (contracts 2 to 5): the camera
// relation, the meter's bucket split, the windowed average, the target and
// the adaptation step. The Exposure object, its frame functions and its
// passes are exposure_passes.cpp's.
namespace bw::postprocess {

namespace {

constexpr float kLogRange = kHistogramLogMax - kHistogramLogMin;
constexpr float kLastBucket = static_cast<float>(kHistogramBuckets - 1U);

// The average held inside the range of contract 5, its ends the averages
// the range's EV100 ends bring to middle grey.
[[nodiscard]] float clamp_average(float luminance, float min_ev100, float max_ev100) noexcept {
  return std::clamp(luminance, average_luminance_at_ev100(min_ev100),
                    average_luminance_at_ev100(max_ev100));
}

} // namespace

// The exposure at an EV100 is 1 / (1.2 * 2^EV100).
float exposure_from_ev100(float ev100) noexcept {
  return 1.0F / (kExposureCalibration * std::exp2(ev100));
}

// The inverse of exposure_from_ev100.
float ev100_from_exposure(float exposure) noexcept {
  return std::log2(1.0F / (kExposureCalibration * exposure));
}

// A bound in EV100 is the luminance kExposureCalibration * 2^ev, times
// 0.18.
float average_luminance_at_ev100(float ev100) noexcept {
  return kMiddleGrey * kExposureCalibration * std::exp2(ev100);
}

// The position is log2 luminance times a scale plus a bias, the scale and
// bias spanning the meter's range; then the two-bucket split and the black
// bucket's weight.
// The lower bound is applied with fmax, so a NaN sample is metered as black
// rather than reaching the integer conversion.
BucketSplit histogram_buckets(float luminance) noexcept {
  const float floor_luminance = std::exp2(kHistogramLogMin);
  const float log_luminance = std::log2(std::fmax(luminance, floor_luminance));
  const float position = std::clamp((log_luminance - kHistogramLogMin) / kLogRange, 0.0F, 1.0F);
  const float bucket = position * kLastBucket;
  const float whole = std::floor(bucket);
  const float fraction = bucket - whole;
  BucketSplit split{};
  split.first = static_cast<std::uint32_t>(whole);
  split.second = std::min(split.first + 1U, kHistogramBuckets - 1U);
  split.second_weight = fraction;
  split.first_weight = split.first == 0U ? 0.0F : 1.0F - fraction;
  return split;
}

// Bucket i sits at position i / (size - 1).
float bucket_log_luminance(std::uint32_t bucket) noexcept {
  return kHistogramLogMin + (static_cast<float>(bucket) / kLastBucket) * kLogRange;
}

// The outliers are removed from the low end, then the high end, by the two
// fractions of the sum; then the mass-weighted mean of the log, or the
// empty histogram's value. The fractions are the constants 0.1 and 0.9, so
// they are never close enough to need a guard.
float windowed_average_luminance(std::span<const float, kHistogramBuckets> histogram,
                                 float min_ev100) noexcept {
  float sum = 0.0F;
  for (const float value : histogram) {
    sum += value;
  }
  float low = sum * kMeterLowFraction;
  float high = sum * kMeterHighFraction;
  float weighted_log = 0.0F;
  float weight = 0.0F;
  for (std::uint32_t i = 0; i < kHistogramBuckets; ++i) {
    float value = histogram[i];
    const float below = std::min(value, low);
    value -= below;
    low -= below;
    high -= below;
    value = std::min(value, high);
    high -= value;
    weighted_log += bucket_log_luminance(i) * value;
    weight += value;
  }
  if (weight == 0.0F) {
    return average_luminance_at_ev100(min_ev100) / kMiddleGrey;
  }
  return std::exp2(weighted_log / weight);
}

// The average clamped, brought to middle grey, raised by the compensation.
float target_exposure(float average_luminance, float compensation_stops, float min_ev100,
                      float max_ev100) noexcept {
  return kMiddleGrey * std::exp2(compensation_stops) /
         clamp_average(average_luminance, min_ev100, max_ev100);
}

// The old exposure from the previous scale and the compensation, the step,
// the clamp to the range, the output scale. The step is exponential or
// linear in log2, with the speed and M chosen by the sign of the
// difference, switching to linear past the start distance; a reset forces
// the target.
// Here the old and target values are the averages the exposure brings to
// middle grey, T and O of contract 5, so the result is middle grey times
// the compensation over the adapted average.
// The compensation is a constant number of stops; no grade carries a curve
// (exposure.hpp).
// The divisor of the output scale needs no floor: the adapted average is
// clamped to the range's lower end first, which is positive.
// previous_exposure > 0 is a precondition (exposure.hpp, contract 5), so a
// previous scale of 0 is not guarded.
float adapt_exposure(const AdaptationInputs& inputs) noexcept {
  const float target = clamp_average(inputs.average_luminance, inputs.min_ev100, inputs.max_ev100);
  const float compensated_grey = kMiddleGrey * std::exp2(inputs.compensation_stops);
  if (inputs.reset) {
    return compensated_grey / target;
  }
  const float log_old = std::log2(compensated_grey / inputs.previous_exposure);
  const float difference = std::log2(target) - log_old;
  const bool brighter = difference > 0.0F;
  const float speed = brighter ? kAdaptationSpeedUp : kAdaptationSpeedDown;
  const float distance = std::fabs(difference);
  float log_adapted = 0.0F;
  if (distance > kAdaptationLinearDistance) {
    const float step = std::min(distance, inputs.delta_seconds * speed);
    log_adapted = log_old + (brighter ? step : -step);
  } else {
    const float m = brighter ? kAdaptationUpM : kAdaptationDownM;
    log_adapted = log_old + difference * (1.0F - std::exp2(-inputs.delta_seconds * speed)) * m;
  }
  return compensated_grey /
         clamp_average(std::exp2(log_adapted), inputs.min_ev100, inputs.max_ev100);
}

} // namespace bw::postprocess
