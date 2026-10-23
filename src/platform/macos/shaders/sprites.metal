// Ordered virtual-canvas quads; source textures contain straight-alpha RGBA.
// Game tint and shared black-fade multiplier arrive in the color uniform.
#include <metal_stdlib>
using namespace metal;
struct SpriteParameters {
  float4 rectangle;
  float4 uv;
  float4 color;
  float4 sampling;
};
struct SpriteVertex {
  float4 position [[position]];
  float2 uv;
};
vertex SpriteVertex sprite_vertex(uint index [[vertex_id]],
                                  constant SpriteParameters& sprite
                                  [[buffer(0)]]) {
  const float2 corners[4] = {float2(0, 0), float2(0, 1), float2(1, 0),
                             float2(1, 1)};
  const float2 corner = corners[index];
  const float2 point = sprite.rectangle.xy + corner * sprite.rectangle.zw;
  return {float4(point.x * 2 - 1, 1 - point.y * 2, 0, 1),
          sprite.uv.xy + corner * sprite.uv.zw};
}
fragment float4 sprite_fragment(SpriteVertex value [[stage_in]],
                                texture2d<float> image [[texture(0)]],
                                constant SpriteParameters& sprite
                                [[buffer(0)]]) {
  constexpr sampler linear_sampler(coord::normalized, address::clamp_to_edge,
                                   filter::linear);
  const float4 sample = image.sample(linear_sampler, value.uv);
  const float4 source =
      sprite.sampling.x > 0
          ? float4(1, 1, 1, sample[uint(sprite.sampling.x) - 1])
          : sample;
  return source * sprite.color;
}
