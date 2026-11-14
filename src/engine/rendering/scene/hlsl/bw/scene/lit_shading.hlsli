// The lit shading model (bw/rendering/scene/forward_pass.hpp, contract 3):
// what the scene renderer does with a lit template's attributes under one
// directional light and a constant ambient term. Durable: the light it is
// called with this slice is the fixture's (fixture_light.hpp).
//
// With N = attributes.normal, V the unit vector from the world position to
// the eye (the same V the template was given), L the light direction,
// H = normalize(V + L):
//   n_l = saturate(N.L), n_h = saturate(N.H), v_h = saturate(V.H),
//   n_v = abs(N.V) + 1e-5;
//   r = clamp(roughness, kMinimumRoughness, 1), a = r * r;
//   f90 = attributes.f90 * saturate(kGrazingFadeScale * max(f0.r, f0.g, f0.b));
//   F = f0 + (f90 - f0) * (1 - v_h)^5;
//   D = a^2 / (pi * (n_h^2 * (a^2 - 1) + 1)^2);
//   Vis = 0.5 / (n_l * sqrt(n_v^2 * (1 - a^2) + a^2) + n_v * sqrt(n_l^2 * (1 - a^2) + a^2));
//   E = f0 * ab.x + f90 * ab.y, with q = r * c0 + c1, t = min(q.x^2,
//     2^(-9.28 * n_v)) * q.x + q.y, ab = (-1.04, 1.04) * t + q.zw;
//   diffuse = diffuse albedo * (1 - dot(E, (0.2126, 0.7152, 0.0722)));
//   M(c, o) = max(o, ((o * (2.0404 * c - 0.3324) + 0.6417 - 4.7951 * c)
//     * o + 2.7552 * c + 0.6903) * o), per channel;
//   white = light color / pi + ambient;
//   color = (diffuse / pi + D * Vis * F) * light color * n_l
//           + ambient * (diffuse albedo * M(diffuse albedo, ambient
//             occlusion) + E * M(f0, ambient occlusion))
//           + emissive * white.
// The two constants repeat forward_pass.hpp's kMinimumRoughness and
// kGrazingFadeScale.
//
// The diffuse is Lambert; the specular is D F G / (4 n.l n.v) with a GGX D,
// a = roughness^2, a Schlick F and the height-correlated masking-shadowing
// term written as the visibility Vis above. The ambient is a sky of one
// luminance in every direction: the surface sends it back by its albedo
// and reflects it by E, and each is occluded by M, the occlusion as light
// that bounces between surfaces of that colour sees it. Occlusion applies
// to that indirect light only. Emissive is a multiple of white, the
// luminance of a surface of albedo 1 facing the light. The F90 fade, the
// energy term and the minimum roughness are stated in forward_pass.hpp.
// There are no image-based or area lights, so no roughness remap for them
// and no fetch by reflected direction. The diffuse is not
// retro-reflective. E is the analytic approximation, not a table, and the
// specular term is not raised for multiple scattering; E only reduces the
// diffuse.
#ifndef BW_SCENE_LIT_SHADING_HLSLI
#define BW_SCENE_LIT_SHADING_HLSLI

static const float bw_lit_pi = 3.14159265358979;
static const float bw_minimum_roughness = 0.04;
static const float bw_grazing_fade_scale = 50.0;

// The occlusion `occlusion` for light that bounces between surfaces of
// colour `color`.
float3 bw_lit_multi_bounce(float3 color, float occlusion) {
  const float3 a = 2.0404 * color - 0.3324;
  const float3 b = -4.7951 * color + 0.6417;
  const float3 c = 2.7552 * color + 0.6903;
  return max(occlusion, ((occlusion * a + b) * occlusion + c) * occlusion);
}

float3 bw_lit_shade(BwMaterialAttributes attributes, float3 view, float3 light_direction,
                    float3 light_color, float3 ambient) {
  const float3 n = attributes.normal;
  const float3 v = view;
  const float3 l = light_direction;
  const float3 h = normalize(v + l);
  const float n_l = saturate(dot(n, l));
  const float n_h = saturate(dot(n, h));
  const float v_h = saturate(dot(v, h));
  const float n_v = abs(dot(n, v)) + 1e-5;

  const float r = clamp(attributes.roughness, bw_minimum_roughness, 1.0);
  const float a = r * r;
  const float a2 = a * a;
  const float3 f0 = attributes.f0;
  const float3 f90 =
      attributes.f90 * saturate(bw_grazing_fade_scale * max(max(f0.r, f0.g), f0.b));

  const float d_denominator = n_h * n_h * (a2 - 1.0) + 1.0;
  const float d = a2 / (bw_lit_pi * d_denominator * d_denominator);
  const float vis = 0.5 / (n_l * sqrt(n_v * n_v * (1.0 - a2) + a2) +
                           n_v * sqrt(n_l * n_l * (1.0 - a2) + a2));
  const float3 f = f0 + (f90 - f0) * pow(1.0 - v_h, 5.0);

  const float4 q = r * float4(-1.0, -0.0275, -0.572, 0.022) + float4(1.0, 0.0425, 1.04, -0.04);
  const float t = min(q.x * q.x, exp2(-9.28 * n_v)) * q.x + q.y;
  const float2 ab = float2(-1.04, 1.04) * t + q.zw;
  const float3 e = f0 * ab.x + f90 * ab.y;
  const float3 diffuse =
      attributes.diffuse_albedo * (1.0 - dot(e, float3(0.2126, 0.7152, 0.0722)));

  const float occlusion = attributes.ambient_occlusion;
  const float3 sky = attributes.diffuse_albedo *
                         bw_lit_multi_bounce(attributes.diffuse_albedo, occlusion) +
                     e * bw_lit_multi_bounce(f0, occlusion);
  const float3 white = light_color / bw_lit_pi + ambient;
  return (diffuse / bw_lit_pi + d * vis * f) * light_color * n_l + ambient * sky +
         attributes.emissive * white;
}

#endif
