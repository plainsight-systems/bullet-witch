#ifndef BW_RENDERING_MATERIALS_LIT_TEMPLATE_HPP
#define BW_RENDERING_MATERIALS_LIT_TEMPLATE_HPP

#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/materials/shading.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"

#include <cstdint>

// Responsibility: the lit material template: its parameter block, its
// static switches, the permutation domain they span with its predicate,
// the vertex attributes each permutation reads, and the attributes it
// writes (docs/architecture/materials.md, invariants 1 to 4 and 6). The
// HLSL is src/engine/rendering/materials/templates/bw/materials/lit.hlsli,
// included by a pass's shaders; it reads a LitMaterial block and writes
// attributes, and holds no lighting.
//
// Contract.
//   1. Switches. Three named static switches, each one define, and the
//      masked dimension, which is set when the material's blend mode is
//      Masked and has no name in an asset:
//        switch                     define                        requires
//        base_color_map             BW_BASE_COLOR_MAP
//        normal_map                 BW_NORMAL_MAP
//        occlusion_reflectance_map  BW_OCCLUSION_REFLECTANCE_MAP  base_color_map
//        (masked)                   BW_MASKED                     base_color_map
//      Each named switch is set when the material has that texture; masked
//      when its blend mode is Masked.
//      lit_switches_admitted is exactly the requires column. The domain is
//      the four dimensions in that order (the first least significant) and
//      one fixed dimension that sets kAttributeMaskDefine to
//      attribute_mask(ShadingModel::Lit); its count is 16, of which
//      kLitAdmittedPermutations (10) are admitted and built.
//   2. Vertex attributes. lit_vertex_attributes(switches) has bit
//      (1 << geometry::VertexAttribute) set for: Position and Normal
//      always; TexCoord0 with base_color_map or normal_map; Tangent with
//      normal_map. The template's shaders decode Position as float3,
//      Normal and Tangent as short4n and the texture coordinate as half2
//      (kLitPositionType and the two beside it); a mesh group that lacks a
//      required attribute, or carries it with another type, is not drawn
//      by this template, and whoever creates the draw fails by name.
//   3. Parameters. LitMaterial is the block; the member names are the
//      asset's parameter names. Defaults: base_color_tint (1, 1, 1, 1),
//      emissive (0, 0, 0), roughness 1, reflectance 0.04,
//      grazing_reflectance 1, fresnel_exponent 5, specular_tint 0,
//      reflectance_base_luminance 0, opacity_mask_threshold 0.5,
//      opacity_facing 1, opacity_grazing 1, opacity_fresnel_exponent 1,
//      every texture index 0. Each texture member is sampled only when
//      its switch is set: base_color with base_color_map, normal with
//      normal_map, occlusion_reflectance with occlusion_reflectance_map.
//      The occlusion_reflectance texture's packing is this template's:
//      ambient occlusion in the first channel, a reflectance mask in the
//      second, nothing read from the others.
//   4. Blend modes drawn: Opaque, Masked, Translucent, Additive.
//   5. Attributes written, per pixel, given the unit vector V from the
//      surface to the eye, which the pass supplies:
//        base = base_color_tint, times the base_color texture at texture
//          coordinate set 0 with base_color_map;
//        diffuse albedo = base.rgb;
//        with masked, opacity mask = base.a - opacity_mask_threshold (a
//          pass discards the pixel when it is below 0);
//        normal = the interpolated vertex normal, unit length; with
//          normal_map, t.x * T + t.y * B + t.z * N normalized, where
//          t.xy = 2 * (the normal texture's first two channels at texture
//          coordinate set 0) - 1, t.z = sqrt(saturate(1 - dot(t.xy, t.xy))),
//          N and T are the interpolated vertex normal and tangent as
//          interpolated, and B = tangent.w * cross(N, T);
//        with n_v = saturate(dot(normal, V)) and
//          curve(facing, grazing, e) = facing + (grazing - facing) *
//          (1 - n_v)^e:
//        roughness = the parameter;
//        r = saturate(curve(reflectance, grazing_reflectance,
//          fresnel_exponent) * mask), where mask = lerp(1,
//          luminance(base.rgb), reflectance_base_luminance), times the
//          occlusion_reflectance texture's second channel at texture
//          coordinate set 0 with occlusion_reflectance_map, and
//          luminance(c) = dot(c, (0.2126, 0.7152, 0.0722));
//        F0 = F90 = r * lerp((1, 1, 1), base.rgb / max(base.r, base.g,
//          base.b, 1e-4), specular_tint): the template evaluates its own
//          curve over the view angle, so the two are equal;
//        opacity = saturate(base.a * curve(opacity_facing,
//          opacity_grazing, opacity_fresnel_exponent));
//        emissive = the emissive parameter times base.rgb;
//        ambient occlusion = 1; with occlusion_reflectance_map, the
//          occlusion_reflectance texture's first channel at texture
//          coordinate set 0.
//      It writes no other attribute.
//   6. lit_material_template() is the template's MaterialTemplate: name
//      kLitTemplate, shading model Lit, the block and defaults of 3, the
//      three switch names in the order of 1, the three textures of 3 with
//      their switches, the sampler member, the blend modes of 4, the
//      permutation of 1 (no value when not admitted) and the attributes
//      of 2.
//   The template has no second base color layer and no blend mask between
//   layers; no draw reaches one. It declares neither a switch nor a
//   parameter for them, so a material that carries a switch for one fails
//   by name when it is created (material.hpp, SwitchUnknown).
//
// A switch is resolved before compilation and selects the shader; a
// numeric or texture parameter never does. Every switch is static; none
// is a dynamic branch.
// The tangent frame: bitangent = sign * cross(normal, tangent), and the
// perturbed normal is built from the interpolated, unnormalized vectors and
// normalized after. The bitangent is rebuilt per pixel, which matches a
// baker that does the same. Nothing is baked by the game, so there is no
// inverse transform back to tangent space.
// curve is a material's own Fresnel term, pow(1 - max(0, N.V), exponent) *
// (1 - fraction) + fraction, with an exponent (default 5) and the fraction
// reflected face-on (default 0.04); a fraction of 1 disables the view
// term. curve names its two ends, for reflectance and for opacity; the
// translucent pass applies no view term of its own.
// The occlusion_reflectance texture's second channel is a per-texel factor
// on reflectance, and the tint colours it. reflectance_base_luminance is a
// material expression over the base color, the tint's hue is base / max
// component, and the luminance weights are Rec. 709.
// The occlusion_reflectance texture's first channel is the occlusion: one
// channel, affecting indirect lighting only.
// The frame needs tangent.w to be the handedness sign, -1 or +1, equal
// over a triangle; a mesh asset carries it.
//
// Read guidance: I.1, Con.5 (the predicate and the attribute set are
// constexpr over the switch values); CACHE.2 (the block is 96 bytes).
namespace bw::materials {

// Offsets: base_color_tint 0, emissive 16, roughness 32, reflectance 36,
// grazing_reflectance 40, fresnel_exponent 44, specular_tint 48,
// reflectance_base_luminance 52, opacity_mask_threshold 56,
// opacity_facing 60, opacity_grazing 64, opacity_fresnel_exponent 68,
// base_color 72, normal 76, occlusion_reflectance 80, sampler 84, two
// reserved words;
// size 96.
struct alignas(shaders::kBlockAlignment) LitMaterial {
  shaders::Float4 base_color_tint{1.0F, 1.0F, 1.0F, 1.0F};
  shaders::Float3 emissive{};
  float roughness{1.0F};
  float reflectance{0.04F};
  float grazing_reflectance{1.0F};
  float fresnel_exponent{5.0F};
  float specular_tint{};
  float reflectance_base_luminance{};
  float opacity_mask_threshold{0.5F};
  float opacity_facing{1.0F};
  float opacity_grazing{1.0F};
  float opacity_fresnel_exponent{1.0F};
  shaders::TextureIndex base_color{};
  shaders::TextureIndex normal{};
  shaders::TextureIndex occlusion_reflectance{};
  shaders::SamplerIndex sampler{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
};
inline constexpr std::uint32_t kLitMaterialBytes = 96;
static_assert(sizeof(LitMaterial) == kLitMaterialBytes);
static_assert(kLitMaterialBytes <= kMaxMaterialBlockBytes);

struct LitSwitches {
  bool base_color_map{};
  bool normal_map{};
  bool occlusion_reflectance_map{};
  bool masked{};
  [[nodiscard]] friend constexpr bool operator==(const LitSwitches&,
                                                 const LitSwitches&) noexcept = default;
};

using LitBaseColorMap = shaders::BoolDimension<"BW_BASE_COLOR_MAP">;
using LitNormalMap = shaders::BoolDimension<"BW_NORMAL_MAP">;
using LitOcclusionReflectanceMap = shaders::BoolDimension<"BW_OCCLUSION_REFLECTANCE_MAP">;
using LitMasked = shaders::BoolDimension<"BW_MASKED">;
// One value: the define is the lit row of the attribute table.
using LitAttributes =
    shaders::IntDimension<kAttributeMaskDefine, 1,
                          static_cast<std::int32_t>(attribute_mask(ShadingModel::Lit))>;
using LitDomain = shaders::PermutationDomain<LitBaseColorMap, LitNormalMap,
                                             LitOcclusionReflectanceMap, LitMasked, LitAttributes>;

[[nodiscard]] constexpr bool lit_switches_admitted(const LitSwitches& s) noexcept {
  return (!s.occlusion_reflectance_map || s.base_color_map) && (!s.masked || s.base_color_map);
}

[[nodiscard]] constexpr LitSwitches lit_switches(const LitDomain& domain) noexcept {
  return LitSwitches{.base_color_map = domain.get<LitBaseColorMap>(),
                     .normal_map = domain.get<LitNormalMap>(),
                     .occlusion_reflectance_map = domain.get<LitOcclusionReflectanceMap>(),
                     .masked = domain.get<LitMasked>()};
}

[[nodiscard]] constexpr LitDomain lit_domain(const LitSwitches& s) noexcept {
  LitDomain domain{};
  domain.set<LitBaseColorMap>(s.base_color_map);
  domain.set<LitNormalMap>(s.normal_map);
  domain.set<LitOcclusionReflectanceMap>(s.occlusion_reflectance_map);
  domain.set<LitMasked>(s.masked);
  return domain;
}

inline constexpr std::uint32_t kLitAdmittedPermutations = 10;
namespace detail {
[[nodiscard]] constexpr std::uint32_t lit_admitted_count() noexcept {
  std::uint32_t admitted = 0;
  for (std::uint32_t id = 0; id < LitDomain::count; ++id) {
    const LitSwitches switches = lit_switches(LitDomain::from_id(shaders::PermutationId{id}));
    if (lit_switches_admitted(switches)) {
      admitted += 1U;
    }
  }
  return admitted;
}
} // namespace detail
static_assert(detail::lit_admitted_count() == kLitAdmittedPermutations,
              "the lit template admits 10 of its 16 switch combinations");

inline constexpr geometry::VertexElementType kLitPositionType = geometry::VertexElementType::Float3;
inline constexpr geometry::VertexElementType kLitFrameType =
    geometry::VertexElementType::Short4Norm;
inline constexpr geometry::VertexElementType kLitTexCoordType = geometry::VertexElementType::Half2;

[[nodiscard]] constexpr std::uint32_t lit_vertex_attributes(const LitSwitches& s) noexcept {
  const auto bit = [](geometry::VertexAttribute a) noexcept {
    return 1U << static_cast<std::uint32_t>(a);
  };
  std::uint32_t mask =
      bit(geometry::VertexAttribute::Position) | bit(geometry::VertexAttribute::Normal);
  if (s.base_color_map || s.normal_map) {
    mask |= bit(geometry::VertexAttribute::TexCoord0);
  }
  if (s.normal_map) {
    mask |= bit(geometry::VertexAttribute::Tangent);
  }
  return mask;
}

[[nodiscard]] const MaterialTemplate& lit_material_template() noexcept;

} // namespace bw::materials

using LitMaterial = bw::materials::LitMaterial;
BW_PARAMETER_BLOCK_LAYOUT(LitMaterial, BW_PARAMETER_MEMBER(LitMaterial, base_color_tint),
                          BW_PARAMETER_MEMBER(LitMaterial, emissive),
                          BW_PARAMETER_MEMBER(LitMaterial, roughness),
                          BW_PARAMETER_MEMBER(LitMaterial, reflectance),
                          BW_PARAMETER_MEMBER(LitMaterial, grazing_reflectance),
                          BW_PARAMETER_MEMBER(LitMaterial, fresnel_exponent),
                          BW_PARAMETER_MEMBER(LitMaterial, specular_tint),
                          BW_PARAMETER_MEMBER(LitMaterial, reflectance_base_luminance),
                          BW_PARAMETER_MEMBER(LitMaterial, opacity_mask_threshold),
                          BW_PARAMETER_MEMBER(LitMaterial, opacity_facing),
                          BW_PARAMETER_MEMBER(LitMaterial, opacity_grazing),
                          BW_PARAMETER_MEMBER(LitMaterial, opacity_fresnel_exponent),
                          BW_PARAMETER_MEMBER(LitMaterial, base_color),
                          BW_PARAMETER_MEMBER(LitMaterial, normal),
                          BW_PARAMETER_MEMBER(LitMaterial, occlusion_reflectance),
                          BW_PARAMETER_MEMBER(LitMaterial, sampler),
                          BW_PARAMETER_MEMBER(LitMaterial, reserved0),
                          BW_PARAMETER_MEMBER(LitMaterial, reserved1));
#endif
