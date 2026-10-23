// Startup movie presentation: aspect-fit geometry is selected by the host.
// Sample decoded BGRA and composite recovered black-overlay opacity.
#include <metal_stdlib>
using namespace metal;
struct Vertex {
  float4 position [[position]];
  float2 uv;
};
vertex Vertex movie_vertex(uint index [[vertex_id]]) {
  const float2 points[4] = {float2(-1, 1), float2(-1, -1), float2(1, 1),
                            float2(1, -1)};
  const float2 uv[4] = {float2(0, 0), float2(0, 1), float2(1, 0), float2(1, 1)};
  return {float4(points[index], 0, 1), uv[index]};
}
fragment float4 movie_fragment(Vertex value [[stage_in]],
                               texture2d<float> frame [[texture(0)]],
                               constant float& opacity [[buffer(0)]]) {
  constexpr sampler linear_sampler(coord::normalized, filter::linear);
  return float4(frame.sample(linear_sampler, value.uv).rgb * opacity, 1);
}
