// The meter and the adaptation as the two exposure kernels compute them
// (bw/rendering/postprocess/exposure.hpp, contracts 3 to 5), with every
// constant the header names stated once here at the header's value. The
// histogram kernel (post_histogram.hlsl) places samples with
// bw_histogram_buckets; the adaptation kernel (post_adaptation.hlsl) takes
// the windowed average and adapts. The C++ functions of exposure.cpp are the
// reference these follow step for step.
#ifndef BW_POSTPROCESS_EXPOSURE_HLSLI
#define BW_POSTPROCESS_EXPOSURE_HLSLI

// exposure.hpp: kHistogramBuckets, kMeterLowFraction, kMeterHighFraction,
// kExposureCalibration, kAdaptationSpeedUp, kAdaptationSpeedDown,
// kAdaptationLinearDistance, kAdaptationUpM, kAdaptationDownM, kMiddleGrey.
// The range the exposure follows the meter in is not a constant: the
// adaptation's block carries it (post_shaders.hpp, PostAdaptation).
#define BW_HISTOGRAM_BUCKETS 64u
static const float BW_METER_LOW_FRACTION = 0.10;
static const float BW_METER_HIGH_FRACTION = 0.90;
static const float BW_EXPOSURE_CALIBRATION = 1.2;
static const float BW_ADAPTATION_SPEED_UP = 3.0;
static const float BW_ADAPTATION_SPEED_DOWN = 1.0;
static const float BW_ADAPTATION_LINEAR_DISTANCE = 1.5;
static const float BW_ADAPTATION_UP_M = 0.97855963;
static const float BW_ADAPTATION_DOWN_M = 0.96736295;
static const float BW_MIDDLE_GREY = 0.18;

// The histogram's buckets hold each weight times this scale, truncated: the
// sums are integers, so they are the same whatever order the threads add in
// (exposure.hpp, contract 7). A bucket is two 32-bit words, a low word and a
// carry word that counts the low word's wraps, so no target size wraps a
// bucket: the carry word wraps only past 2^32 * 2^32 / 2^19 = 2^45 samples
// of full weight. bw_bucket_value reads the pair as a float of samples.
// Weights are packed at 2^19, a 13.19 fixed point.
static const float BW_HISTOGRAM_FIXED_POINT = 524288.0;
// Bytes per bucket in the histogram's buffer: the low word, then the carry.
#define BW_HISTOGRAM_BUCKET_BYTES 8u

float bw_bucket_value(uint low, uint carry) {
  return float(carry) * (4294967296.0 / BW_HISTOGRAM_FIXED_POINT) +
         float(low) * (1.0 / BW_HISTOGRAM_FIXED_POINT);
}

// The position is log2 luminance times a scale plus a bias spanning the
// meter's range; then the two-bucket split and the black bucket's weight.
// The lower bound is applied with max, so a NaN sample meters as black.
// log_min and log_max are the meter's range, kHistogramLogMin and
// kHistogramLogMax, which the blocks carry.
void bw_histogram_buckets(float luminance, float log_min, float log_max, out uint first,
                          out uint second, out float first_weight, out float second_weight) {
  const float log_luminance = log2(max(luminance, exp2(log_min)));
  const float position = saturate((log_luminance - log_min) / (log_max - log_min));
  const float bucket = position * float(BW_HISTOGRAM_BUCKETS - 1u);
  const float whole = floor(bucket);
  const float fraction = bucket - whole;
  first = uint(whole);
  second = min(first + 1u, BW_HISTOGRAM_BUCKETS - 1u);
  second_weight = fraction;
  first_weight = first == 0u ? 0.0 : 1.0 - fraction;
}

// Bucket i sits at position i / (size - 1).
float bw_bucket_log_luminance(uint bucket, float log_min, float log_max) {
  return log_min + (float(bucket) / float(BW_HISTOGRAM_BUCKETS - 1u)) * (log_max - log_min);
}

// exposure.hpp, average_luminance_at_ev100: a bound in EV100 is the
// luminance kExposureCalibration * 2^ev, times 0.18.
float bw_average_luminance_at_ev100(float ev100) {
  return BW_MIDDLE_GREY * BW_EXPOSURE_CALIBRATION * exp2(ev100);
}

// The average held inside the range of exposure.hpp contract 5.
float bw_clamp_average(float luminance, float min_ev100, float max_ev100) {
  return clamp(luminance, bw_average_luminance_at_ev100(min_ev100),
               bw_average_luminance_at_ev100(max_ev100));
}

// The step is exponential or linear in log2, with the speed and M chosen by
// the sign of the difference, switching to linear past the start distance;
// a reset forces the target. The old exposure comes from the previous scale
// and the compensation, then the clamp to the range and the output scale.
// The compensation is a constant number of stops; no grade carries a curve.
// The output scale's divisor needs no floor: the adapted average is clamped
// to the range's lower end first, which is positive.
// A previous scale of 0 is not guarded: the texels are made holding a
// positive factor and only this function writes them.
float bw_adapt_exposure(float previous_exposure, float average_luminance, float compensation_stops,
                        float delta_seconds, bool reset, float min_ev100, float max_ev100) {
  const float target = bw_clamp_average(average_luminance, min_ev100, max_ev100);
  const float compensated_grey = BW_MIDDLE_GREY * exp2(compensation_stops);
  if (reset) {
    return compensated_grey / target;
  }
  const float log_old = log2(compensated_grey / previous_exposure);
  const float difference = log2(target) - log_old;
  const bool brighter = difference > 0.0;
  const float speed = brighter ? BW_ADAPTATION_SPEED_UP : BW_ADAPTATION_SPEED_DOWN;
  const float distance = abs(difference);
  float log_adapted;
  if (distance > BW_ADAPTATION_LINEAR_DISTANCE) {
    const float step = min(distance, delta_seconds * speed);
    log_adapted = log_old + (brighter ? step : -step);
  } else {
    const float m = brighter ? BW_ADAPTATION_UP_M : BW_ADAPTATION_DOWN_M;
    log_adapted = log_old + difference * (1.0 - exp2(-delta_seconds * speed)) * m;
  }
  return compensated_grey / bw_clamp_average(exp2(log_adapted), min_ev100, max_ev100);
}

#endif
