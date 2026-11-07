#ifndef BW_RENDERING_MATERIALS_SHADING_HPP
#define BW_RENDERING_MATERIALS_SHADING_HPP

#include <cstdint>

// Responsibility: the shading models, the blend modes, the attributes a
// material template may write, and the one table that says which
// attributes each shading model reads under each blend mode
// (docs/architecture/materials.md, "Attributes" and invariant 6). A
// template declares one shading model; the mask of its row is what its
// shaders are compiled against, so the attribute set a template can write
// comes from this table and nowhere else.
//
// Contract.
//   1. attribute_active(attribute, model, blend) is the table of
//      materials.md, "Attributes", cell by cell: an empty cell is false; a
//      plain yes is true for every blend mode; "translucent blend modes"
//      is true for Translucent, Additive and Modulate; "masked blend mode"
//      and "masked" are true for Masked; "opaque and masked" is true for
//      Opaque and Masked. Subsurface and hair read opacity under every
//      blend mode.
//   2. attribute_mask(model) has bit (1 << attribute) set for each
//      attribute that attribute_active gives true for under at least one
//      blend mode. A template's shaders receive this value as the define
//      kAttributeMaskDefine and declare an attribute only when its bit is
//      set, so a template that writes an attribute outside its model's row
//      does not compile, and the compiler names the attribute.
//   The enumerators' values are this module's own and are part of the
//   cooked material's blend_mode parameter (material.hpp); they do not
//   change once content carries them.
//
// Each attribute is one decision from the shading model and the blend
// mode; the table of materials.md decides it. There is no anisotropy,
// refraction, custom data, customized UV, displacement, surface thickness
// or shading-model-from-expression attribute, and no post-process, light
// function, decal, volume or UI domain: no template the game needs reads
// one. Roughness and reflectance do not depend on a translucency lighting
// mode, which the forward pass does not have. Reflectance is carried by
// the three attributes described at the enumeration, not by base color,
// metallic and specular.
//
// Read guidance: Enum.2 and Enum.3 (three scoped enumerations), Con.5 (the
// table is a constexpr function, no data to initialize).
namespace bw::materials {

// The shading models a template the game needs uses; materials.md adds a
// row when a template needs another.
enum class ShadingModel : std::uint8_t {
  Unlit = 0,
  Lit = 1,
  Subsurface = 2,
  Hair = 3,
};
inline constexpr std::uint32_t kShadingModelCount = 4;

// The blend modes the content's markers map to; there is no alpha
// composite, alpha holdout or coloured transmittance mode.
enum class BlendMode : std::uint8_t {
  Opaque = 0,
  Masked = 1,
  Translucent = 2,
  Additive = 3,
  Modulate = 4,
};
inline constexpr std::uint32_t kBlendModeCount = 5;

// A surface is a diffuse albedo plus F0 and F90 colours: reflectance is
// carried as the reflectance at normal incidence and at grazing, each a
// colour, so a reflection tinted by the surface and a grazing value below
// one both have a place. A template written in base color, metallic and
// specular terms writes F0 = lerp(0.08 * specular, base color, metallic),
// F90 = 1 and diffuse albedo = base color * (1 - metallic).
enum class MaterialAttribute : std::uint8_t {
  DiffuseAlbedo = 0,
  F0 = 1,
  F90 = 2,
  Roughness = 3,
  Normal = 4,
  Tangent = 5,
  Emissive = 6,
  AmbientOcclusion = 7,
  SubsurfaceColor = 8,
  Opacity = 9,
  OpacityMask = 10,
  WorldPositionOffset = 11,
  PixelDepthOffset = 12,
};
inline constexpr std::uint32_t kMaterialAttributeCount = 13;

// The define a template's shaders receive attribute_mask(model) under.
inline constexpr char kAttributeMaskDefine[] = "BW_MATERIAL_ATTRIBUTES";

[[nodiscard]] constexpr bool attribute_active(MaterialAttribute attribute, ShadingModel model,
                                              BlendMode blend) noexcept {
  const bool lit = model == ShadingModel::Lit;
  const bool unlit = model == ShadingModel::Unlit;
  const bool subsurface = model == ShadingModel::Subsurface;
  const bool hair = model == ShadingModel::Hair;
  const bool masked = blend == BlendMode::Masked;
  const bool translucent = blend != BlendMode::Opaque && blend != BlendMode::Masked;
  switch (attribute) {
  case MaterialAttribute::DiffuseAlbedo:
  case MaterialAttribute::F0:
  case MaterialAttribute::F90:
  case MaterialAttribute::Roughness:
  case MaterialAttribute::Normal:
  case MaterialAttribute::AmbientOcclusion:
    return lit || subsurface || hair;
  case MaterialAttribute::Tangent:
    return hair;
  case MaterialAttribute::Emissive:
  case MaterialAttribute::WorldPositionOffset:
    return true;
  case MaterialAttribute::SubsurfaceColor:
    return subsurface;
  case MaterialAttribute::Opacity:
    return (lit || unlit) ? translucent : true;
  case MaterialAttribute::OpacityMask:
    return masked;
  case MaterialAttribute::PixelDepthOffset:
    return !translucent;
  }
  return false;
}

[[nodiscard]] constexpr std::uint32_t attribute_mask(ShadingModel model) noexcept {
  std::uint32_t mask = 0;
  for (std::uint32_t a = 0; a < kMaterialAttributeCount; ++a) {
    for (std::uint32_t b = 0; b < kBlendModeCount; ++b) {
      if (attribute_active(static_cast<MaterialAttribute>(a), model, static_cast<BlendMode>(b))) {
        mask |= 1U << a;
      }
    }
  }
  return mask;
}

} // namespace bw::materials

#endif
