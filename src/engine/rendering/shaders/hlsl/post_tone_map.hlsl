// The tone mapping pass (bw/rendering/postprocess/tone_mapping.hpp,
// contracts 2 to 4; post_shaders.hpp, PostToneMap): the one place scene
// colour becomes display colour.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// PostToneMapShader): pixel, entry main, per-draw block PostToneMap, no
// static block, no buffer block. Drawn with post_fullscreen.hlsl into the
// target.
//
// In order, for the pixel whose centre is at uv: scene colour c, bounded by
// kSceneColorMax (post_shaders.hpp, contract 1), and the bloom result b
// sampled at uv; s, the adapted texel over the factor texel;
// m, the vignette mask; x = (c + b * bloom_intensity) * s * m; the table
// sampled at the lookup coordinate of the shaper encode of x, times
// kLookupScale; plus one triangular noise value times dither_amplitude.
// Nothing after the lookup encodes: the table holds encoded values.
//
// There is no film grain: it needs a tiling texture this module does not
// have, and a grade with grain does not validate. The vignette has one value
// and the lens is centred. The table is built for an sRGB surface only.
// Alpha is 1 (tone_mapping.hpp, Notes).
#include "bw/bindless.hlsli"
#include "bw/generated/post_tone_map.hlsli"
#include "bw/postprocess/fullscreen.hlsli"
#include "bw/postprocess/grade.hlsli"
#include "bw/postprocess/scene_color.hlsli"

// tone_mapping.hpp, vignette_mask.
float bw_vignette_mask(float2 uv, float aspect, float intensity) {
  if (intensity <= 0.0) {
    return 1.0;
  }
  const float scale = sqrt(2.0) / sqrt(1.0 + aspect * aspect);
  const float2 q = (uv * 2.0 - 1.0) * float2(1.0, aspect) * scale * intensity;
  const float inverse = 1.0 / (dot(q, q) + 1.0);
  return inverse * inverse;
}

// tone_mapping.hpp, "The noise": a uniform hash of the pixel's uv and the
// frame's offset, made triangular in -1 to 1.
float bw_triangular_noise(float2 uv) {
  const float2 p = uv + PostToneMap.dither_offset;
  const float uniform_value = frac(sin(p.x + p.y * 543.31) * 493013.0);
  const float w = uniform_value * 2.0 - 1.0;
  const float s = w > 0.0 ? 1.0 : (w < 0.0 ? -1.0 : 0.0);
  return s * (1.0 - sqrt(saturate(1.0 - abs(w))));
}

float4 main(BwFullscreenVaryings input) : SV_Target0 {
  const float2 uv = input.uv;
  const float3 scene = min(
      BW_TEXTURE2D(PostToneMap.scene_color).SampleLevel(BW_SAMPLER(PostToneMap.sampler), uv, 0.0).rgb,
      BW_SCENE_COLOR_MAX);
  const float3 bloom =
      BW_TEXTURE2D(PostToneMap.bloom).SampleLevel(BW_SAMPLER(PostToneMap.sampler), uv, 0.0).rgb;
  const float factor = BW_TEXTURE2D(PostToneMap.factor).Load(int3(0, 0, 0)).r;
  const float adapted = BW_TEXTURE2D(PostToneMap.adapted).Load(int3(0, 0, 0)).r;
  const float aspect = PostToneMap.target_size.y / PostToneMap.target_size.x;
  const float mask = bw_vignette_mask(uv, aspect, PostToneMap.vignette);

  const float3 exposed = (scene + bloom * PostToneMap.bloom_intensity) * (adapted / factor) * mask;
  const float3 coordinate = bw_lookup_coordinate(bw_shaper_encode(exposed));
  const float3 display = BW_TEXTURE3D(PostToneMap.table)
                             .SampleLevel(BW_SAMPLER(PostToneMap.sampler), coordinate, 0.0)
                             .rgb *
                         BW_LOOKUP_SCALE;
  return float4(display + bw_triangular_noise(uv) * PostToneMap.dither_amplitude, 1.0);
}
