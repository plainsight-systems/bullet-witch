// Transform static geometry and encode projected depth with original constant
// and slope bias. Caller supplies position scale/offset and four clip rows.
// This pass verifies geometry addressing and depth behavior independently of
// textured surfaces. Provenance: registry vertex 2 / pixel 0; equations and
// bytecode identity are recorded in world-renderer-foundation.md.
#include <metal_stdlib>
using namespace metal;
struct WorldDepthParameters {
  float4 position_scale;
  float4 position_offset;
  float4 clip_rows[4];
  float4 depth_bias;
};
struct WorldStreamParameters {
  uint stride;
  uint position_offset;
};
struct WorldDepthVertex {
  float4 position [[position]];
  float2 clip_depth;
};
vertex WorldDepthVertex world_depth_vertex(
    uint vertex_id [[vertex_id]], device const uchar* vertices [[buffer(0)]],
    constant WorldDepthParameters& parameters [[buffer(1)]],
    constant WorldStreamParameters& stream [[buffer(2)]]) {
  // Preserve the stored stream stride; only POSITION0 float3 is read here.
  const device packed_float3* source =
      reinterpret_cast<const device packed_float3*>(
          vertices + vertex_id * stream.stride + stream.position_offset);
  const float4 p = float4(float3(*source), 1.0f) * parameters.position_scale +
                   parameters.position_offset;
  const float4 clip =
      float4(dot(p, parameters.clip_rows[0]), dot(p, parameters.clip_rows[1]),
             dot(p, parameters.clip_rows[2]), dot(p, parameters.clip_rows[3]));
  return {clip, clip.zw};
}
fragment float4 world_depth_fragment(WorldDepthVertex input [[stage_in]],
                                     constant WorldDepthParameters& parameters
                                     [[buffer(1)]]) {
  // Pixel0 biases the color result while ordinary hardware depth remains
  // intact.
  const float projected_depth = input.clip_depth.x / input.clip_depth.y;
  const float slope =
      max(abs(dfdx(projected_depth)), abs(dfdy(projected_depth)));
  return float4(projected_depth + parameters.depth_bias.x +
                parameters.depth_bias.y * slope);
}
