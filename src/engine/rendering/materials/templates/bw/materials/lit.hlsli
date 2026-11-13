// The lit material template (bw/rendering/materials/lit_template.hpp): it
// reads a LitMaterial block and the inputs a pass gives it per pixel, and
// writes attributes (bw/materials/attributes.hlsli). It holds no lighting:
// the pass shades what it writes (bw/rendering/scene/forward_pass.hpp).
//
// Defines, one per switch of lit_template.hpp, contract 1: BW_BASE_COLOR_MAP,
// BW_NORMAL_MAP, BW_OCCLUSION_REFLECTANCE_MAP, BW_MASKED, and
// BW_MATERIAL_ATTRIBUTES, the lit row of the attribute table. A shader
// includes bw/bindless.hlsli, its generated include (which declares
// BW_LitMaterial) and bw/materials/attributes.hlsli first.
//
// Attributes written, lit_template.hpp, contract 5, given the unit vector V
// from the surface to the eye:
//   base = base_color_tint, times the base_color texture at texture
//     coordinate set 0 with BW_BASE_COLOR_MAP;
//   diffuse albedo = base.rgb; with BW_MASKED, opacity mask = base.a -
//     opacity_mask_threshold;
//   normal = the interpolated vertex normal, unit length; with
//     BW_NORMAL_MAP, normalize(t.x * T + t.y * B + t.z * N), t.xy = 2 * (the
//     normal texture's first two channels) - 1, t.z = sqrt(saturate(1 -
//     dot(t.xy, t.xy))), N and T the interpolated normal and tangent as
//     interpolated, B = tangent.w * cross(N, T);
//   n_v = saturate(dot(normal, V)), curve(facing, grazing, e) = facing +
//     (grazing - facing) * (1 - n_v)^e;
//   roughness = the parameter;
//   r = saturate(curve(reflectance, grazing_reflectance, fresnel_exponent)
//     * mask), mask = lerp(1, luminance(base.rgb), reflectance_base_luminance)
//     times the occlusion_reflectance texture's second channel with
//     BW_OCCLUSION_REFLECTANCE_MAP, luminance weights (0.2126, 0.7152,
//     0.0722);
//   F0 = F90 = r * lerp(1, base.rgb / max(base.r, base.g, base.b, 1e-4),
//     specular_tint);
//   opacity = saturate(base.a * curve(opacity_facing, opacity_grazing,
//     opacity_fresnel_exponent));
//   emissive = the emissive parameter times base.rgb;
//   ambient occlusion = 1, or the occlusion_reflectance texture's first
//     channel with BW_OCCLUSION_REFLECTANCE_MAP.
// x^0 is 1 for every x, 0 included, as the curve's arithmetic reads it; the
// curve states that case itself because pow lowers to exp2(e * log2(x)),
// which is not a number at x = 0, e = 0.
//
// The tangent frame: bitangent = sign * cross(normal, tangent), rebuilt per
// pixel, and the perturbed normal is built from the interpolated,
// unnormalized vectors and normalized after. The game bakes nothing, so
// there is no inverse transform back to tangent space.
#ifndef BW_MATERIALS_LIT_HLSLI
#define BW_MATERIALS_LIT_HLSLI

#if !defined(BW_BASE_COLOR_MAP) || !defined(BW_NORMAL_MAP) ||                                     \
    !defined(BW_OCCLUSION_REFLECTANCE_MAP) || !defined(BW_MASKED)
#error "the lit template's shaders are compiled with its four switch defines"
#endif

// What a pass gives the template for one pixel, in world space.
struct BwLitPixel {
  float3 normal;
  // The unit vector from the surface to the eye.
  float3 view;
#if BW_NORMAL_MAP
  // xyz as interpolated, w the bitangent sign.
  float4 tangent;
#endif
#if BW_BASE_COLOR_MAP || BW_NORMAL_MAP
  float2 uv0;
#endif
};

float bw_lit_curve(float facing, float grazing, float exponent, float n_v) {
  const float shape = exponent == 0.0 ? 1.0 : pow(1.0 - n_v, exponent);
  return facing + (grazing - facing) * shape;
}

// base for the pixel at texture coordinate set 0 `uv0`; uv0 is read only
// with BW_BASE_COLOR_MAP. The forward pass and the depth pass's masked
// pixel stage both take base and the opacity mask from these two
// functions, so the two passes keep and discard the same pixels.
float4 bw_lit_base(BW_LitMaterial material, float2 uv0) {
  float4 base = material.base_color_tint;
#if BW_BASE_COLOR_MAP
  base *= BW_TEXTURE2D(material.base_color).Sample(BW_SAMPLER(material.sampler), uv0);
#endif
  return base;
}

#if BW_MASKED
float bw_lit_opacity_mask(BW_LitMaterial material, float4 base) {
  return base.a - material.opacity_mask_threshold;
}
#endif

BwMaterialAttributes bw_lit_attributes(BW_LitMaterial material, BwLitPixel pixel) {
  BwMaterialAttributes attributes = (BwMaterialAttributes)0;
#if BW_BASE_COLOR_MAP || BW_NORMAL_MAP
  const float4 base = bw_lit_base(material, pixel.uv0);
#else
  const float4 base = bw_lit_base(material, float2(0.0, 0.0));
#endif
  attributes.diffuse_albedo = base.rgb;
#if BW_MASKED
  attributes.opacity_mask = bw_lit_opacity_mask(material, base);
#endif
#if BW_NORMAL_MAP
  const float2 t_xy =
      2.0 * BW_TEXTURE2D(material.normal).Sample(BW_SAMPLER(material.sampler), pixel.uv0).xy -
      1.0;
  const float t_z = sqrt(saturate(1.0 - dot(t_xy, t_xy)));
  const float3 n = pixel.normal;
  const float3 t = pixel.tangent.xyz;
  const float3 b = pixel.tangent.w * cross(n, t);
  attributes.normal = normalize(t_xy.x * t + t_xy.y * b + t_z * n);
#else
  attributes.normal = normalize(pixel.normal);
#endif
  const float n_v = saturate(dot(attributes.normal, pixel.view));

  float mask = lerp(1.0, dot(base.rgb, float3(0.2126, 0.7152, 0.0722)),
                    material.reflectance_base_luminance);
  float ambient_occlusion = 1.0;
#if BW_OCCLUSION_REFLECTANCE_MAP
  const float2 packed = BW_TEXTURE2D(material.occlusion_reflectance)
                            .Sample(BW_SAMPLER(material.sampler), pixel.uv0)
                            .xy;
  ambient_occlusion = packed.x;
  mask *= packed.y;
#endif
  const float r = saturate(bw_lit_curve(material.reflectance, material.grazing_reflectance,
                                        material.fresnel_exponent, n_v) *
                           mask);
  const float3 hue = base.rgb / max(max(base.r, base.g), max(base.b, 1e-4));
  const float3 reflectance = r * lerp(float3(1.0, 1.0, 1.0), hue, material.specular_tint);
  attributes.f0 = reflectance;
  attributes.f90 = reflectance;
  attributes.roughness = material.roughness;
  attributes.opacity =
      saturate(base.a * bw_lit_curve(material.opacity_facing, material.opacity_grazing,
                                     material.opacity_fresnel_exponent, n_v));
  attributes.emissive = material.emissive * base.rgb;
  attributes.ambient_occlusion = ambient_occlusion;
  return attributes;
}

#endif
