// One level of the bloom chain's way down (bw/rendering/postprocess/
// bloom.hpp, "The filter", Down; post_shaders.hpp, PostBloomDownsample).
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// PostBloomDownsampleShader): pixel, entry main, per-draw block
// PostBloomDownsample, no static block, no buffer block. Drawn with
// post_fullscreen.hlsl into the level.
//
// Thirteen bilinear fetches of the source, clamped at the edge by the
// block's sampler, around the point of the source under the pixel's centre
// (its uv), at offsets in source texels of source_texel. Five groups of
// four: the centre group at (+-1, +-1) weighs 0.5, and the four two by two
// blocks of the grid {-2, 0, 2} squared weigh 0.125 each. A group's value is
// the mean of its fetches; with `firefly` set (level 0) it is their mean
// weighted by 1 / (1 + Y), Y the Rec. 709 luminance of the fetch as stored,
// and the groups still combine by their weights. Every fetch is bounded by
// kSceneColorMax (post_shaders.hpp, contract 1): on level 0 the source is
// scene colour, whose texels a blend may have taken past the bound; the
// levels below hold less than the bound already, so the same bound leaves
// them alone.
//
// The luminance's coefficients are the Rec. 709 ones (bloom.hpp).
#include "bw/bindless.hlsli"
#include "bw/generated/post_bloom_downsample.hlsli"
#include "bw/postprocess/fullscreen.hlsli"
#include "bw/postprocess/scene_color.hlsli"

static const float3 BW_REC709_LUMINANCE = float3(0.2126, 0.7152, 0.0722);

float3 bw_fetch(float2 uv, float2 offset) {
  const float3 texel = BW_TEXTURE2D(PostBloomDownsample.source)
                           .SampleLevel(BW_SAMPLER(PostBloomDownsample.sampler),
                                        uv + offset * PostBloomDownsample.source_texel, 0.0)
                           .rgb;
  return min(texel, BW_SCENE_COLOR_MAX);
}

// A group of four fetches: their mean, or with the firefly weight their
// weighted mean.
float3 bw_group(float3 a, float3 b, float3 c, float3 d) {
  if (PostBloomDownsample.firefly == 0u) {
    return (a + b + c + d) * 0.25;
  }
  const float wa = 1.0 / (1.0 + dot(a, BW_REC709_LUMINANCE));
  const float wb = 1.0 / (1.0 + dot(b, BW_REC709_LUMINANCE));
  const float wc = 1.0 / (1.0 + dot(c, BW_REC709_LUMINANCE));
  const float wd = 1.0 / (1.0 + dot(d, BW_REC709_LUMINANCE));
  return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

float4 main(BwFullscreenVaryings input) : SV_Target0 {
  const float2 uv = input.uv;
  const float3 nw = bw_fetch(uv, float2(-2.0, -2.0));
  const float3 n = bw_fetch(uv, float2(0.0, -2.0));
  const float3 ne = bw_fetch(uv, float2(2.0, -2.0));
  const float3 w = bw_fetch(uv, float2(-2.0, 0.0));
  const float3 c = bw_fetch(uv, float2(0.0, 0.0));
  const float3 e = bw_fetch(uv, float2(2.0, 0.0));
  const float3 sw = bw_fetch(uv, float2(-2.0, 2.0));
  const float3 s = bw_fetch(uv, float2(0.0, 2.0));
  const float3 se = bw_fetch(uv, float2(2.0, 2.0));
  const float3 inner_nw = bw_fetch(uv, float2(-1.0, -1.0));
  const float3 inner_ne = bw_fetch(uv, float2(1.0, -1.0));
  const float3 inner_sw = bw_fetch(uv, float2(-1.0, 1.0));
  const float3 inner_se = bw_fetch(uv, float2(1.0, 1.0));

  const float3 centre = bw_group(inner_nw, inner_ne, inner_sw, inner_se);
  const float3 top_left = bw_group(nw, n, w, c);
  const float3 top_right = bw_group(n, ne, c, e);
  const float3 bottom_left = bw_group(w, c, sw, s);
  const float3 bottom_right = bw_group(c, e, s, se);
  const float3 color = centre * 0.5 + (top_left + top_right + bottom_left + bottom_right) * 0.125;
  return float4(color, 1.0);
}
