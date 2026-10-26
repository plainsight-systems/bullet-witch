// Source program admission and dependency decoding for every rendering target.
// The header defines ownership, costs and both-phase I.1/CDSA.32/GPU.4 use.
// Semantic feature selection follows original consumed constants, samplers and
// vertex declarations; it does not infer a shader from the scene owner type.
#include "bw/render/materials/world_material.hpp"

#include <algorithm>
#include <array>

namespace bw::render {
namespace {
// Compatibility identities only: flags in the upper 32 bits, variant below.
// These exact keys have independent instruction/declaration witnesses. Keep
// unaudited combinations closed even when their feature bits look familiar.
constexpr std::array<std::uint64_t, 158> kAuditedPrograms{
    0x008000000000ULL, 0x008002000000ULL, 0x00800a020001ULL, 0x00800a820001ULL,
    0x00800a830001ULL, 0x008204900001ULL, 0x008206900001ULL, 0x008208820102ULL,
    0x00820a020001ULL, 0x00820a820001ULL, 0x00820a820102ULL, 0x00820a830001ULL,
    0x008224900001ULL, 0x008224910001ULL, 0x008226900001ULL, 0x008226910001ULL,
    0x00822a820001ULL, 0x00a20a820001ULL, 0x00a22aa20001ULL, 0x00c00a020001ULL,
    0x00c00a030001ULL, 0x00c00a820001ULL, 0x00c00a830001ULL, 0x00c20a020001ULL,
    0x00c20a820001ULL, 0x00c20a830001ULL, 0x00c22a820001ULL, 0x00e008020001ULL,
    0x00e008030001ULL, 0x00e008820001ULL, 0x00e008820102ULL, 0x00e008830001ULL,
    0x00e00a020001ULL, 0x00e00a030001ULL, 0x00e00a820001ULL, 0x00e00a820102ULL,
    0x00e00a830001ULL, 0x00e208020001ULL, 0x00e208030001ULL, 0x00e208820001ULL,
    0x00e208830001ULL, 0x00e20a020001ULL, 0x00e20a030001ULL, 0x00e20a820001ULL,
    0x00e20a830001ULL, 0x00e228820001ULL, 0x00e228a20001ULL, 0x00e22a820001ULL,
    0x00e22aa20001ULL, 0x02800a020001ULL, 0x02a00a030001ULL, 0x02c00a020001ULL,
    0x02c00a030001ULL, 0x02c00a820001ULL, 0x02e008020001ULL, 0x02e008030001ULL,
    0x02e008820001ULL, 0x02e008820102ULL, 0x02e008830001ULL, 0x02e00a020001ULL,
    0x02e00a030001ULL, 0x02e00a820001ULL, 0x02e00a820102ULL, 0x02e00a830001ULL,
    0x04a008820001ULL, 0x04a008830001ULL, 0x04a00a820001ULL, 0x04a00a830001ULL,
    0x04a20a820001ULL, 0x04a20a830001ULL, 0x04e208820001ULL, 0x04e208830001ULL,
    0x04e20a820001ULL, 0x04e20a830001ULL, 0x06c002000001ULL, 0x06c00a020001ULL,
    0x06c00a030001ULL, 0x06c00a820001ULL, 0x06e000000001ULL, 0x06e000010001ULL,
    0x06e002000001ULL, 0x06e002010001ULL, 0x06e008020001ULL, 0x06e008030001ULL,
    0x06e008820001ULL, 0x06e00a020001ULL, 0x06e00a030001ULL, 0x06e00a820001ULL,
    0x408000100001ULL, 0x408002100001ULL, 0x408004110001ULL, 0x408004900001ULL,
    0x408006110001ULL, 0x408006900001ULL, 0x428000100001ULL, 0x428000910001ULL,
    0x428002100001ULL, 0x428002910001ULL, 0x808000100001ULL, 0x808000110001ULL,
    0x808000900001ULL, 0x808002100001ULL, 0x808002110001ULL, 0x808002900001ULL,
    0x808004900001ULL, 0x808004910001ULL, 0x808006900001ULL, 0x808006910001ULL,
    0x808220100001ULL, 0x808220110001ULL, 0x808222100001ULL, 0x808222110001ULL,
    0x808224900001ULL, 0x808224910001ULL, 0x808226900001ULL, 0x808226910001ULL,
    0x828000100001ULL, 0x828000110001ULL, 0x828002100001ULL, 0x828002110001ULL,
    0x828004100001ULL, 0x828004900001ULL, 0x828004910001ULL, 0x828006100001ULL,
    0x828006900001ULL, 0x828006910001ULL, 0x82c080b00002ULL, 0x82c082b00002ULL,
    0xc08000100001ULL, 0xc08000110001ULL, 0xc08000900001ULL, 0xc08002100001ULL,
    0xc08002110001ULL, 0xc08002900001ULL, 0xc08004100001ULL, 0xc08004900001ULL,
    0xc08004910001ULL, 0xc08006100001ULL, 0xc08006900001ULL, 0xc08006910001ULL,
    0xc08020100001ULL, 0xc08020110001ULL, 0xc08022100001ULL, 0xc08022110001ULL,
    0xc08024900001ULL, 0xc08026900001ULL, 0xc08220100001ULL, 0xc08220110001ULL,
    0xc08222100001ULL, 0xc08222110001ULL, 0xc0c220100001ULL, 0xc0c222100001ULL,
    0xc28000100001ULL, 0xc28000110001ULL, 0xc28002100001ULL, 0xc28002110001ULL,
    0xc28004100001ULL, 0xc28006100001ULL,
};
static_assert(std::ranges::is_sorted(kAuditedPrograms));
}  // namespace

WorldMaterialRequirements world_material_requirements(
    std::uint32_t effective_flags, std::uint16_t variant,
    WorldLightingPolicy lighting) noexcept {
  if (lighting != WorldLightingPolicy::supplied &&
      lighting != WorldLightingPolicy::without_direct_lighting) {
    return {};
  }
  const auto key = (std::uint64_t{effective_flags} << 16) | variant;
  if (!std::ranges::binary_search(kAuditedPrograms, key)) return {};

  constexpr std::uint16_t kInterpolatedLayer = 2, kModulatedLayer = 0x102;
  constexpr unsigned kInfluenceShift = 30;
  constexpr std::uint32_t kDepthCutout = 0x1, kSkinned = 0x10;
  constexpr std::uint32_t kVertexColor = 0x20, kNormal = 0x80;
  constexpr std::uint32_t kIrradiance = 0x200, kOcclusion = 0x400;
  constexpr std::uint32_t kVertexIllumination = 0x800, kEnvironment = 0x2000;
  constexpr std::uint32_t kAlphaFresnel = 0x8000, kFresnel = 0x20000;
  constexpr std::uint32_t kDisableShadow = 0x200000, kDisableLocal = 0x400000;
  constexpr std::uint32_t kDisableReflection = 0x2000000;
  constexpr std::uint32_t kDisableDirectional = 0x4000000;
  const auto has = [effective_flags](std::uint32_t mask) {
    return (effective_flags & mask) != 0;
  };
  const bool direct_energy = lighting == WorldLightingPolicy::supplied;
  WorldMaterialRequirements result;
  result.supported = true;
  result.directional_light = direct_energy && !has(kDisableDirectional);
  result.local_lights = direct_energy && !has(kDisableLocal);
  result.environment_map = has(kEnvironment);
  result.spherical_harmonics = has(kIrradiance);
  result.alpha_fresnel = has(kAlphaFresnel);

  // Local-only surfaces retain specular, and normal-map use follows all normal
  // consumers. Directional disable does not disable those other source paths.
  result.reflection = !has(kDisableReflection) &&
                      (result.directional_light || result.local_lights ||
                       result.environment_map);
  result.fresnel = result.reflection && has(kFresnel);
  result.normal_required = result.directional_light || result.local_lights ||
                           result.spherical_harmonics ||
                           result.environment_map || result.alpha_fresnel;
  result.normal_map = has(kNormal) && result.normal_required;
  result.shadow_reception = result.directional_light && !has(kDisableShadow);
  result.environment_shadow = result.environment_map && result.shadow_reception;

  // Layer variant identifies the original input set: zero has no texture/UV,
  // one uses base0, two interpolates clothing, 0x102 modulates a second layer.
  result.base_texture = variant != 0;
  result.secondary_base =
      variant == kInterpolatedLayer || variant == kModulatedLayer;
  result.secondary_alpha_blend = variant == kInterpolatedLayer;
  result.vertex_color = has(kVertexColor);
  result.vertex_illumination = has(kVertexIllumination);
  result.ambient_occlusion = has(kOcclusion);
  result.depth_cutout = has(kDepthCutout);
  result.joint_influences =
      has(kSkinned)
          ? static_cast<std::uint8_t>(4U - (effective_flags >> kInfluenceShift))
          : 0;
  return result;
}
}  // namespace bw::render
