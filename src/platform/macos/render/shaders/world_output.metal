// Original scene HDR to display chain: log luminance, bounded reductions,
// threshold/soft-knee bloom and its recovered filters, exposure and gamma.
// Pass sizes, sampler footprints and equations are mapped in
// world-display-output.md.
#include <metal_stdlib>
using namespace metal;
struct OutputVertex {
  float4 position [[position]];
  float2 uv;
};
struct OutputParameters {
  float2 inverse_extent;
  float exposure_key, inverse_gamma;
  float bloom_key, bloom_threshold, bloom_soft_knee, log_luminance;
  uint adapted, bloom, enabled;
  float opacity;
  float4 global_color;
  float4 first_measurement_bounds;
};
constexpr sampler point_clamp(coord::normalized, address::clamp_to_edge,
                              filter::nearest);
constexpr sampler linear_clamp(coord::normalized, address::clamp_to_edge,
                               filter::linear);
vertex OutputVertex world_output_vertex(uint id [[vertex_id]],
                                        constant OutputParameters& p
                                        [[buffer(0)]]) {
  constexpr float2 positions[] = {{-1, 1}, {-1, -1}, {1, 1}, {1, -1}};
  constexpr float2 uvs[] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
  // Original D3D9 quad samples at (pixel + .25) / destination extent.
  // Metal samples at pixel + .5, so move this quad right/down by .25 pixels.
  // Copying the original negative clip offset would move in the wrong
  // direction.
  float2 position = positions[id] + float2(.5, -.5) * p.inverse_extent;
  return {float4(position, 0, 1), uvs[id]};
}
// Original ScalingRGB pool pixel 17: point-clamped RGB multiplication,
// unchanged alpha, no saturation. The separate RGBA16F target precedes the
// luminance/bloom chain, preserving both measurement input and quantization.
fragment float4 world_scale_rgb(OutputVertex in [[stage_in]],
                                texture2d<float> image [[texture(0)]],
                                constant OutputParameters& p [[buffer(0)]]) {
  float4 color = image.sample(point_clamp, in.uv);
  return float4(color.rgb * p.global_color.rgb, color.a);
}
fragment float4 world_log_luminance(OutputVertex in [[stage_in]],
                                    texture2d<float> image [[texture(0)]]) {
  float3 color = image.sample(linear_clamp, in.uv).rgb;
  return float4(color, log(dot(color, float3(.299, .587, .114)) + .0001));
}
float4 average_four(texture2d<float> image, float2 uv, float2 step) {
  return (image.sample(point_clamp, uv + float2(-.5, -.5) * step) +
          image.sample(point_clamp, uv + float2(.5, -.5) * step) +
          image.sample(point_clamp, uv + float2(-.5, .5) * step) +
          image.sample(point_clamp, uv + float2(.5, .5) * step)) *
         .25;
}
fragment float4 world_downsample(OutputVertex in [[stage_in]],
                                 texture2d<float> image [[texture(0)]],
                                 constant OutputParameters& p [[buffer(0)]]) {
  return average_four(image, in.uv, p.inverse_extent);
}
fragment float world_downsample_luminance(OutputVertex in [[stage_in]],
                                          texture2d<float> image [[texture(0)]],
                                          constant OutputParameters& p
                                          [[buffer(0)]]) {
  return average_four(image, in.uv, p.inverse_extent).a;
}
fragment float world_reduce_luminance(OutputVertex in [[stage_in]],
                                      texture2d<float> image [[texture(0)]],
                                      constant OutputParameters& p
                                      [[buffer(0)]]) {
  float sum = 0;
  for (uint y = 0; y < 4; ++y)
    for (uint x = 0; x < 4; ++x)
      sum += image
                 .sample(point_clamp,
                         in.uv + (float2(x, y) - 1.5) * p.inverse_extent)
                 .r;
  return sum / 16;
}
// With no completed CPU readback, apply the same source output limits to the
// actual current GPU measurement. No temporal sample is fabricated. A disabled
// history keeps the original raw-reduction path; retained CPU output is already
// bounded. Bloom and final transfer intentionally differ after this selection.
float output_log_luminance(texture2d<float> luminance,
                           constant OutputParameters& p) {
  if (p.adapted) return p.log_luminance;
  float measured = luminance.sample(point_clamp, float2(.5)).r;
  if (p.first_measurement_bounds.w != 0) {
    float value = max(exp(measured), p.first_measurement_bounds.z);
    measured = log(clamp(value, p.first_measurement_bounds.x,
                         p.first_measurement_bounds.y));
  }
  return measured;
}
fragment float4 world_bloom_extract(OutputVertex in [[stage_in]],
                                    texture2d<float> image [[texture(0)]],
                                    texture2d<float> luminance [[texture(1)]],
                                    constant OutputParameters& p
                                    [[buffer(0)]]) {
  float measured = output_log_luminance(luminance, p);
  float3 thresholded = max(image.sample(point_clamp, in.uv).rgb * p.bloom_key /
                                   exp(measured + .001) -
                               p.bloom_threshold,
                           0.0);
  return float4(thresholded / (thresholded + p.bloom_soft_knee), 1);
}
fragment float4 world_bloom_radial(OutputVertex in [[stage_in]],
                                   texture2d<float> image [[texture(0)]],
                                   constant OutputParameters& p [[buffer(0)]]) {
  float4 sum = 0;
  float total = 0;
  for (int x = -2; x <= 2; ++x)
    for (int y = -2; y <= 2; ++y)
      if (abs(x) + abs(y) <= 2) {
        float weight = exp(-.5 * float(x * x + y * y));
        sum +=
            image.sample(point_clamp, in.uv + float2(x, y) * p.inverse_extent) *
            weight;
        total += weight;
      }
  return sum / total;
}
float4 gaussian_axis(texture2d<float> image, float2 uv, float2 axis) {
  // Original sigma=3, multiplier=2, unnormalized fifteen-tap weights.
  float4 sum = 0;
  for (int i = -7; i <= 7; ++i) {
    float weight = 2 * exp(-float(i * i) / 18.0) / sqrt(18.0 * M_PI_F);
    sum += image.sample(point_clamp, uv + float(i) * axis) *
           float4(weight, weight, weight, 1);
  }
  return sum;
}
fragment float4 world_bloom_horizontal(OutputVertex in [[stage_in]],
                                       texture2d<float> image [[texture(0)]],
                                       constant OutputParameters& p
                                       [[buffer(0)]]) {
  return gaussian_axis(image, in.uv, float2(p.inverse_extent.x, 0));
}
fragment float4 world_bloom_vertical(OutputVertex in [[stage_in]],
                                     texture2d<float> image [[texture(0)]],
                                     constant OutputParameters& p
                                     [[buffer(0)]]) {
  return gaussian_axis(image, in.uv, float2(0, p.inverse_extent.y));
}
fragment float4 world_display_output(OutputVertex in [[stage_in]],
                                     texture2d<float> image [[texture(0)]],
                                     texture2d<float> luminance [[texture(1)]],
                                     texture2d<float> bloom [[texture(2)]],
                                     constant OutputParameters& p
                                     [[buffer(0)]]) {
  float3 color = image.sample(point_clamp, in.uv).rgb;
  if (p.enabled) {
    float measured = output_log_luminance(luminance, p);
    color = pow(abs(color * p.exposure_key / (exp(measured) + .001)),
                float3(p.inverse_gamma));
    if (p.bloom) color += bloom.sample(linear_clamp, in.uv).rgb;
  }
  return float4(color * p.opacity, 1);
}

// Original final-copy edge program: integer-pixel fullscreen UVs, distinct
// from the quarter-pixel postprocess quad above. Shift raster geometry to
// translate D3D9 integer centers to Metal half-integer centers.
vertex OutputVertex world_final_edge_vertex(uint id [[vertex_id]],
                                            constant OutputParameters& p
                                            [[buffer(0)]]) {
  constexpr float2 positions[] = {{-1, 1}, {-1, -1}, {1, 1}, {1, -1}};
  constexpr float2 uvs[] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
  const float2 position = positions[id] + float2(1, -1) * p.inverse_extent;
  return {float4(position, 0, 1), uvs[id]};
}
fragment float4 world_final_edge(OutputVertex in [[stage_in]],
                                 texture2d<float> image [[texture(0)]],
                                 constant OutputParameters& p [[buffer(0)]]) {
  // Preserve the original diagonal bias and bounded two/four-tap selection.
  // The represented preceding tone/RGB passes leave slot0 point-clamped.
  const float3 luminance = float3(.299, .587, .114);
  const float2 half_step = .5f * p.inverse_extent;
  const float nw =
      dot(image.sample(point_clamp, in.uv - half_step).rgb, luminance);
  const float ne =
      dot(image.sample(point_clamp, in.uv + float2(half_step.x, -half_step.y))
              .rgb,
          luminance) +
      .0026041669771f;
  const float sw = dot(
      image.sample(point_clamp, in.uv + float2(-half_step.x, half_step.y)).rgb,
      luminance);
  const float se =
      dot(image.sample(point_clamp, in.uv + half_step).rgb, luminance);
  const float2 gradient = float2((sw - ne) + (se - nw), (sw - ne) - (se - nw));
  const float2 direction = gradient * rsqrt(dot(gradient, gradient));
  const float2 outer_step =
      clamp(direction / (8 * min(abs(direction.x), abs(direction.y))), -2.0f,
            2.0f) *
      (2 * p.inverse_extent);
  const float4 outer = image.sample(point_clamp, in.uv + outer_step) +
                       image.sample(point_clamp, in.uv - outer_step);
  const float4 inner =
      image.sample(point_clamp, in.uv + direction * half_step) +
      image.sample(point_clamp, in.uv - direction * half_step);
  const float4 four = (outer + inner) * .25f;
  const float low = min(min(se, ne), min(sw, nw));
  const float high = max(max(ne, se), max(nw, sw));
  const float filtered_luminance = dot(four.rgb, luminance);
  const float4 filtered = filtered_luminance < low || filtered_luminance > high
                              ? inner * .5f
                              : four;
  const float4 center = image.sample(point_clamp, in.uv);
  const float center_luminance = dot(center.rgb, luminance);
  const float contrast =
      max(high, center_luminance) - min(low, center_luminance);
  const float4 color = contrast >= max(high * .125f, .05f) ? filtered : center;
  return float4(color.rgb * p.opacity, color.a);
}
