// Classify only source-traced surface and actor material families for every
// target. The exact supported selectors preserve original program dependencies;
// descriptive feature requirements keep compatibility bits out of scene code.
// Contract/algorithm and both-phase E.6/GPU.1 guidance are in the header.
#include "bw/render/world_material.hpp"

#include <algorithm>
#include <array>

namespace bw::render {
WorldMaterialRequirements world_material_requirements(
    std::uint32_t effective_flags, std::uint16_t variant,
    WorldLightingPolicy lighting) noexcept {
  // The selector shares color programs but chooses a cutout depth program.
  // Preserve that coverage requirement separately from color equations.
  constexpr std::uint32_t kAlphaSelector = 1;
  constexpr std::uint32_t kSphericalHarmonics = 0x200;
  const bool irradiance = (effective_flags & kSphericalHarmonics) != 0;
  const auto flags = effective_flags & ~(kAlphaSelector | kSphericalHarmonics);
  constexpr std::uint16_t kOneBase = 1, kTwoBases = 0x102;
  constexpr std::uint32_t kNormal = 0x80, kEnvironment = 0x2000;
  constexpr std::uint32_t kVertexColor = 0x20, kVertexIllumination = 0x800;
  constexpr std::uint32_t kFresnel = 0x20000, kDisableReflection = 0x2000000;
  constexpr std::uint32_t kDisableDirectional = 0x4000000;
  constexpr std::uint32_t kSingleJoint = 0x40000000;
  // These exact families share the retained material equations. Directional
  // terms now consume the explicit shadow map; the restricted observation
  // policy still omits direct energy. Local-light lists remain separate.
  constexpr std::uint32_t kLayeredShadowSurface = 0x00820882;
  constexpr std::uint32_t kLocalLightSurface = 0x04a00882;
  if (lighting == WorldLightingPolicy::without_direct_lighting ||
      lighting == WorldLightingPolicy::supplied) {
    // This clothing program interpolates between two base colors; its
    // Fresnel coefficient controls alpha rather than reflection energy.
    constexpr std::uint32_t kClothingLayer = 0x82c080b0;
    constexpr std::uint16_t kClothingVariant = 2;
    if (flags == kClothingLayer && variant == kClothingVariant) {
      WorldMaterialRequirements result;
      result.supported = true;
      result.normal_map = true;
      result.secondary_base = true;
      result.vertex_color = true;
      result.directional_light = lighting == WorldLightingPolicy::supplied;
      result.spherical_harmonics = irradiance;
      result.joint_influences = 2;
      result.secondary_alpha_blend = true;
      result.alpha_fresnel = true;
      return result;
    }
    const bool layered = flags == kLayeredShadowSurface && variant == kTwoBases;
    const bool local = flags == kLocalLightSurface && variant == kOneBase;
    const bool untextured = flags == 0x00800000 && variant == 0;
    constexpr std::array<std::uint32_t, 9> kBackgroundSelectors{
        0xc0800010, 0x80800010, 0xc0822010, 0xc2800010, 0x80800090,
        0x82800010, 0xc0802010, 0xc0c22010, 0xc0800090};
    // These actor programs share the source surface equations with an
    // optional authored ambient-occlusion map and one/two/four bone inputs.
    // Their direct local-light list is supplied by the prepared scene owner.
    constexpr std::array<std::uint32_t, 18> kActorSelectors{
        0x00820490, 0x00822490, 0x40800010, 0x40800410, 0x42800090, 0x80800490,
        0x80822010, 0x80822490, 0x82800010, 0x82800410, 0x82800490, 0xc0800010,
        0xc0800410, 0xc0800490, 0xc0802490, 0xc2800410, 0x40800490, 0x42800010};
    if (variant == kOneBase &&
        std::ranges::find(kActorSelectors, flags) != kActorSelectors.end()) {
      WorldMaterialRequirements result;
      result.supported = true;
      result.directional_light = lighting == WorldLightingPolicy::supplied;
      result.normal_map = (flags & kNormal) != 0;
      result.environment_map = (flags & kEnvironment) != 0;
      result.environment_shadow = result.environment_map;
      result.reflection =
          (result.environment_map || result.directional_light) &&
          (flags & kDisableReflection) == 0;
      result.fresnel = result.reflection && (flags & kFresnel) != 0;
      result.alpha_cutout = (effective_flags & kAlphaSelector) != 0;
      result.spherical_harmonics = irradiance;
      constexpr std::uint32_t kTwoJoints = 0x80000000, kOcclusion = 0x400;
      result.local_lights = lighting == WorldLightingPolicy::supplied;
      result.joint_influences = (flags & kSingleJoint) != 0 ? 1
                                : (flags & kTwoJoints) != 0 ? 2
                                                            : 4;
      result.ambient_occlusion = (flags & kOcclusion) != 0;
      return result;
    }

    if (variant == kOneBase && std::ranges::find(kBackgroundSelectors, flags) !=
                                   kBackgroundSelectors.end()) {
      WorldMaterialRequirements result;
      result.supported = true;
      result.directional_light = lighting == WorldLightingPolicy::supplied;
      result.normal_map = (flags & kNormal) != 0;
      result.environment_map = (flags & kEnvironment) != 0;
      result.environment_shadow = result.environment_map;
      result.reflection =
          (result.environment_map || result.directional_light) &&
          (flags & kDisableReflection) == 0;
      result.fresnel = result.reflection && (flags & kFresnel) != 0;
      result.alpha_cutout = (effective_flags & kAlphaSelector) != 0;
      result.spherical_harmonics = irradiance;
      result.joint_influences = (flags & kSingleJoint) != 0 ? 1 : 2;
      return result;
    }

    if (layered || local || untextured) {
      WorldMaterialRequirements result;
      result.supported = true;
      result.secondary_base = layered;
      result.vertex_illumination = !untextured;
      result.alpha_cutout = (effective_flags & kAlphaSelector) != 0;
      result.spherical_harmonics = irradiance;
      result.directional_light =
          lighting == WorldLightingPolicy::supplied && !local;
      result.normal_map =
          (irradiance || result.directional_light) && !untextured;
      result.reflection = result.directional_light;
      result.fresnel = result.reflection && (flags & kFresnel) != 0;
      result.base_texture = !untextured;
      // The untextured source program consumes the scene-wide direct packet.
      result.local_lights =
          untextured && lighting == WorldLightingPolicy::supplied;
      return result;
    }
  }
  if (lighting != WorldLightingPolicy::supplied) return {};
  constexpr std::array<std::uint32_t, 12> kSingleBaseSelectors{
      0x00e20802, 0x00e20882, 0x00e00802, 0x00e00882, 0x02e00802, 0x02e00882,
      0x06e00802, 0x06e00882, 0x04e20882, 0x00e22882, 0x00e228a2, 0x06e00000};
  constexpr std::array<std::uint32_t, 2> kLayeredBaseSelectors{0x00e00882,
                                                               0x02e00882};
  const bool supported =
      (variant == kOneBase && std::ranges::find(kSingleBaseSelectors, flags) !=
                                  kSingleBaseSelectors.end()) ||
      (variant == kTwoBases &&
       std::ranges::find(kLayeredBaseSelectors, flags) !=
           kLayeredBaseSelectors.end());
  if (!supported) return {};
  // Programs with directional lighting disabled also omit the specular path.
  const bool directional = (flags & kDisableDirectional) == 0;
  const bool reflection = directional && (flags & kDisableReflection) == 0;
  return {.supported = true,
          .normal_map = (flags & kNormal) != 0 && (directional || irradiance),
          .secondary_base = variant == kTwoBases,
          .environment_map = (flags & kEnvironment) != 0,
          .vertex_color = (flags & kVertexColor) != 0,
          .vertex_illumination = (flags & kVertexIllumination) != 0,
          .reflection = reflection,
          .fresnel = reflection && (flags & kFresnel) != 0,
          .directional_light = directional,
          .alpha_cutout = (effective_flags & kAlphaSelector) != 0,
          .spherical_harmonics = irradiance};
}
}  // namespace bw::render
