// Classify only source-traced static material families for every target.
// The exact supported selectors preserve original program dependencies;
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
  // These cached programs contain shadow/local-light work unavailable to the
  // inspection pass. With those light contributions omitted, their remaining
  // material, vertex illumination and fog equations are source-traced directly.
  constexpr std::uint32_t kLayeredShadowSurface = 0x00820882;
  constexpr std::uint32_t kLocalLightSurface = 0x04a00882;
  if (lighting == WorldLightingPolicy::without_direct_lighting) {
    const bool layered = flags == kLayeredShadowSurface && variant == kTwoBases;
    const bool local = flags == kLocalLightSurface && variant == kOneBase;
    if (!layered && !local) return {};
    WorldMaterialRequirements result;
    result.supported = true;
    result.secondary_base = layered;
    result.vertex_illumination = true;
    result.alpha_cutout = (effective_flags & kAlphaSelector) != 0;
    result.spherical_harmonics = irradiance;
    result.normal_map = irradiance;
    return result;
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
  constexpr std::uint32_t kNormal = 0x80, kEnvironment = 0x2000;
  constexpr std::uint32_t kVertexColor = 0x20, kVertexIllumination = 0x800;
  constexpr std::uint32_t kFresnel = 0x20000, kDisableReflection = 0x2000000;
  constexpr std::uint32_t kDisableDirectional = 0x4000000;
  // Programs with directional lighting disabled also omit the specular path.
  const bool directional = (flags & kDisableDirectional) == 0;
  const bool reflection = directional && (flags & kDisableReflection) == 0;
  return {true,
          (flags & kNormal) != 0 && (directional || irradiance),
          variant == kTwoBases,
          (flags & kEnvironment) != 0,
          (flags & kVertexColor) != 0,
          (flags & kVertexIllumination) != 0,
          reflection,
          reflection && (flags & kFresnel) != 0,
          directional,
          (effective_flags & kAlphaSelector) != 0,
          irradiance};
}
}  // namespace bw::render
