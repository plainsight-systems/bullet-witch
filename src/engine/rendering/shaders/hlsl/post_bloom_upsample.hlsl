// One level of the bloom chain's way up (bw/rendering/postprocess/bloom.hpp,
// "The filter", Up; post_shaders.hpp, PostBloomUpsample).
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// PostBloomUpsampleShader): pixel, entry main, per-draw block
// PostBloomUpsample, no static block, no buffer block. Drawn with
// post_fullscreen.hlsl into U_k.
//
// U_k = D_k * level_scale + (below filtered at U_k's size) * below_scale:
// D_k is `level` sampled at the pixel's uv; `below` is U_(k+1), or D_5
// itself on the first pass up, filtered by the three by three tent
// (1, 2, 1; 2, 4, 2; 1, 2, 1) / 16 of bilinear fetches at offsets -1, 0 and
// 1 times `radius` in below's texels, clamped at the edge by the block's
// sampler. level_scale is 1 / kBloomLevels on every pass, and below_scale
// is 1 / kBloomLevels on the first pass, which reads D_5 unscaled, and 1 on
// the others, which read a sum already scaled. So each level carries its
// sixth as it is added and no stored sum is larger than the field it came
// from.
#include "bw/bindless.hlsli"
#include "bw/generated/post_bloom_upsample.hlsli"
#include "bw/postprocess/fullscreen.hlsli"

float3 bw_below(float2 uv, float2 offset) {
  return BW_TEXTURE2D(PostBloomUpsample.below)
      .SampleLevel(BW_SAMPLER(PostBloomUpsample.sampler),
                   uv + offset * PostBloomUpsample.radius * PostBloomUpsample.below_texel, 0.0)
      .rgb;
}

float4 main(BwFullscreenVaryings input) : SV_Target0 {
  const float2 uv = input.uv;
  const float3 corners = bw_below(uv, float2(-1.0, -1.0)) + bw_below(uv, float2(1.0, -1.0)) +
                         bw_below(uv, float2(-1.0, 1.0)) + bw_below(uv, float2(1.0, 1.0));
  const float3 edges = bw_below(uv, float2(0.0, -1.0)) + bw_below(uv, float2(-1.0, 0.0)) +
                       bw_below(uv, float2(1.0, 0.0)) + bw_below(uv, float2(0.0, 1.0));
  const float3 tent = (corners + edges * 2.0 + bw_below(uv, float2(0.0, 0.0)) * 4.0) / 16.0;
  const float3 level = BW_TEXTURE2D(PostBloomUpsample.level)
                           .SampleLevel(BW_SAMPLER(PostBloomUpsample.sampler), uv, 0.0)
                           .rgb;
  return float4(level * PostBloomUpsample.level_scale + tent * PostBloomUpsample.below_scale, 1.0);
}
