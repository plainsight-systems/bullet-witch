// The exposure meter's histogram (bw/rendering/postprocess/exposure.hpp,
// contracts 3 and 7; post_shaders.hpp, PostHistogram). 8 by 8 threads a
// group (exposure.cpp, kHistogramGroupSize), each thread one sample of the
// metered image.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// PostHistogramShader): compute, entry main, per-draw block PostHistogram,
// no static block, no buffer block.
//
// The histogram is kHistogramBuckets buckets of two 32-bit words, a low
// word and a carry word (bw/postprocess/exposure.hlsli), at the start of the
// storage buffer PostHistogram.histogram. With mode kHistogramClear one
// group zeroes them; with kHistogramAccumulate each thread takes sample
// (x, y), the mean of scene colour's texels (2x, 2y) to (2x + 1, 2y + 1),
// each coordinate clamped to the last column and row and each texel bounded
// by kSceneColorMax, meters the mean of its three channels with the factor
// divided out, and adds its two weights into the group's own histogram; the
// group then adds its histogram into the buffer's low words, counting each
// wrap of a low word in its carry word. Every weight is an integer in the
// fixed point of bw/postprocess/exposure.hlsli, so the sums do not depend on
// the order the threads add in. A group's own histogram is one word a
// bucket: 64 samples of weight at most 1 are at most 2^6 * 2^19 = 2^25.
//
// The counts are not normalised: the windowed average does not depend on the
// histogram's scale, so the adaptation kernel reads the integer words. The
// metered value has no luminance floor; the lowest bucket's lower bound
// subsumes it. The two by two mean is a half resolution downsample at an even
// size (exposure.hpp, Notes).
#include "bw/bindless.hlsli"
#include "bw/generated/post_histogram.hlsli"
#include "bw/postprocess/exposure.hlsli"
#include "bw/postprocess/scene_color.hlsli"

groupshared uint bw_group_histogram[BW_HISTOGRAM_BUCKETS];

float3 bw_scene_texel(uint x, uint y) {
  const uint cx = min(x, PostHistogram.width - 1u);
  const uint cy = min(y, PostHistogram.height - 1u);
  const float3 texel = BW_TEXTURE2D(PostHistogram.scene_color).Load(int3(int(cx), int(cy), 0)).rgb;
  return min(texel, BW_SCENE_COLOR_MAX);
}

[numthreads(8, 8, 1)] void main(uint3 thread : SV_DispatchThreadID, uint index : SV_GroupIndex) {
  if (PostHistogram.mode == 0u) {
    if (index < BW_HISTOGRAM_BUCKETS) {
      BW_STORAGE_BUFFER(PostHistogram.histogram).Store(index * BW_HISTOGRAM_BUCKET_BYTES, 0u);
      BW_STORAGE_BUFFER(PostHistogram.histogram).Store(index * BW_HISTOGRAM_BUCKET_BYTES + 4u, 0u);
    }
    return;
  }
  if (index < BW_HISTOGRAM_BUCKETS) {
    bw_group_histogram[index] = 0u;
  }
  GroupMemoryBarrierWithGroupSync();

  const uint samples_x = (PostHistogram.width + 1u) / 2u;
  const uint samples_y = (PostHistogram.height + 1u) / 2u;
  if (thread.x < samples_x && thread.y < samples_y) {
    const uint x = thread.x * 2u;
    const uint y = thread.y * 2u;
    const float3 sum = bw_scene_texel(x, y) + bw_scene_texel(x + 1u, y) +
                       bw_scene_texel(x, y + 1u) + bw_scene_texel(x + 1u, y + 1u);
    const float factor = BW_TEXTURE2D(PostHistogram.factor).Load(int3(0, 0, 0)).r;
    const float luminance = (sum.r + sum.g + sum.b) / (12.0 * factor);
    uint first;
    uint second;
    float first_weight;
    float second_weight;
    bw_histogram_buckets(luminance, PostHistogram.log_min, PostHistogram.log_max, first, second,
                         first_weight, second_weight);
    InterlockedAdd(bw_group_histogram[first], uint(first_weight * BW_HISTOGRAM_FIXED_POINT));
    InterlockedAdd(bw_group_histogram[second], uint(second_weight * BW_HISTOGRAM_FIXED_POINT));
  }
  GroupMemoryBarrierWithGroupSync();

  if (index < BW_HISTOGRAM_BUCKETS) {
    const uint value = bw_group_histogram[index];
    if (value != 0u) {
      uint previous;
      BW_STORAGE_BUFFER(PostHistogram.histogram)
          .InterlockedAdd(index * BW_HISTOGRAM_BUCKET_BYTES, value, previous);
      // previous + value passed 0xffffffff: the low word wrapped.
      if (0xffffffffu - value < previous) {
        BW_STORAGE_BUFFER(PostHistogram.histogram)
            .InterlockedAdd(index * BW_HISTOGRAM_BUCKET_BYTES + 4u, 1u);
      }
    }
  }
}
