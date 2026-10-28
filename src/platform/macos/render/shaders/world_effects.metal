// Original sprite variant 0: world position, per-particle color and UV are
// prepared by CPU simulation. ColorRate and distance fog are source inputs;
// texture modulation, alpha rejection and ordered blending remain on GPU.
#include <metal_stdlib>
using namespace metal;
struct EffectVertex {
  packed_float3 position;
  packed_float4 color;
  packed_float2 uv;
};
struct EffectParameters {
  float4 clip_rows[4];
  float4 color_rate;
  float4 fog;
};
struct EffectRaster {
  float4 position [[position]];
  float4 color;
  float2 uv;
};
vertex EffectRaster world_effect_vertex(uint index [[vertex_id]],
                                        device const EffectVertex* vertices
                                        [[buffer(0)]],
                                        constant EffectParameters& parameters
                                        [[buffer(1)]]) {
  const EffectVertex source = vertices[index];
  const float4 point = float4(float3(source.position), 1);
  const float4 clip = float4(
      dot(point, parameters.clip_rows[0]), dot(point, parameters.clip_rows[1]),
      dot(point, parameters.clip_rows[2]), dot(point, parameters.clip_rows[3]));
  const float attenuation =
      clamp((parameters.fog.x - clip.w) * parameters.fog.y, 0.02F, 1.0F);
  return {clip,
          float4(float3(source.color.xyz) * parameters.color_rate.xyz,
                 source.color.w * attenuation),
          float2(source.uv)};
}
fragment float4 world_effect_fragment(EffectRaster value [[stage_in]],
                                      texture2d<float> image [[texture(0)]]) {
  constexpr sampler source_sampler(coord::normalized, address::repeat,
                                   filter::linear, mip_filter::linear);
  const float4 result = image.sample(source_sampler, value.uv) * value.color;
  if (result.a <= 2.0F / 255.0F) discard_fragment();
  return result;
}
