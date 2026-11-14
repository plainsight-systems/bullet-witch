// The exposure's adaptation (bw/rendering/postprocess/exposure.hpp,
// contracts 4 to 7; post_shaders.hpp, PostAdaptation): one thread reads the
// frame's histogram, takes its windowed average and writes the adapted
// exposure into the texel the frame writes.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// PostAdaptationShader): compute, entry main, per-draw block
// PostAdaptation, no static block, no buffer block.
//
// The histogram is the kHistogramBuckets buckets post_histogram.hlsl added,
// each a low word and a carry word, read through the buffer array and
// unpacked to a float (bw/postprocess/exposure.hlsli, bw_bucket_value); the
// windowed average does not depend on their scale. The range the exposure
// follows the meter in is the block's min_ev100 and max_ev100. The previous exposure is the red channel of the texel the frame reads
// as its factor; the result is written to the red channel of the other.
//
// The two fractions of the sum are the constants 0.1 and 0.9, so the window
// between them never collapses and needs no guard.
#include "bw/bindless.hlsli"
#include "bw/generated/post_adaptation.hlsli"
#include "bw/postprocess/exposure.hlsli"

float bw_windowed_average_luminance(float min_ev100) {
  float histogram[BW_HISTOGRAM_BUCKETS];
  float sum = 0.0;
  for (uint i = 0u; i < BW_HISTOGRAM_BUCKETS; ++i) {
    const uint low = BW_BUFFER(PostAdaptation.histogram).Load(i * BW_HISTOGRAM_BUCKET_BYTES);
    const uint carry = BW_BUFFER(PostAdaptation.histogram).Load(i * BW_HISTOGRAM_BUCKET_BYTES + 4u);
    histogram[i] = bw_bucket_value(low, carry);
    sum += histogram[i];
  }
  float low = sum * BW_METER_LOW_FRACTION;
  float high = sum * BW_METER_HIGH_FRACTION;
  float weighted_log = 0.0;
  float weight = 0.0;
  for (uint b = 0u; b < BW_HISTOGRAM_BUCKETS; ++b) {
    float value = histogram[b];
    const float below = min(value, low);
    value -= below;
    low -= below;
    high -= below;
    value = min(value, high);
    high -= value;
    weighted_log +=
        bw_bucket_log_luminance(b, PostAdaptation.log_min, PostAdaptation.log_max) * value;
    weight += value;
  }
  if (weight == 0.0) {
    return bw_average_luminance_at_ev100(min_ev100) / BW_MIDDLE_GREY;
  }
  return exp2(weighted_log / weight);
}

[numthreads(1, 1, 1)] void main() {
  const float previous = BW_TEXTURE2D(PostAdaptation.previous).Load(int3(0, 0, 0)).r;
  const float adapted = bw_adapt_exposure(
      previous, bw_windowed_average_luminance(PostAdaptation.min_ev100),
      PostAdaptation.compensation_stops, PostAdaptation.delta_seconds, PostAdaptation.reset != 0u,
      PostAdaptation.min_ev100, PostAdaptation.max_ev100);
  BW_STORAGE_TEXTURE2D(PostAdaptation.adapted)[uint2(0u, 0u)] = float4(adapted, 0.0, 0.0, 0.0);
}
