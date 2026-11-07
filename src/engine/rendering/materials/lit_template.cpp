#include "bw/rendering/materials/lit_template.hpp"

#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/materials/shading.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>

// Responsibility: the out-of-line part of bw/rendering/materials/lit_template.hpp:
// the lit template's MaterialTemplate (contract 6).
//
// Contract: lit_material_template() is one immutable object with static
// storage duration. Its defaults are the bytes of a value-initialized
// LitMaterial copied member by member, so the bytes outside every member
// (Float3's fourth word) are zero, as material.hpp, rule 3, requires of a
// block. Its permutation function maps the three switch values, in
// kSwitches order, and the blend mode onto lit_domain, the masked dimension
// set exactly when the blend mode is Masked, and gives no value when
// lit_switches_admitted rejects them.
//
// A switch's value is resolved before compilation and selects the shader;
// numeric and texture parameters do not.
namespace bw::materials {
namespace {

constexpr std::array<std::string_view, 3> kSwitches{kBaseColorMapSwitch, kNormalMapSwitch,
                                                    kOcclusionReflectanceMapSwitch};
// Each texture with the index in kSwitches of the switch that samples it.
constexpr std::array<MaterialTextureParameter, 3> kTextures{{
    {kBaseColorTexture, 0},
    {kNormalTexture, 1},
    {kOcclusionReflectanceTexture, 2},
}};
constexpr std::string_view kSamplerMember = "sampler";
constexpr std::uint32_t kLitBlendModes =
    (1U << static_cast<std::uint32_t>(BlendMode::Opaque)) |
    (1U << static_cast<std::uint32_t>(BlendMode::Masked)) |
    (1U << static_cast<std::uint32_t>(BlendMode::Translucent)) |
    (1U << static_cast<std::uint32_t>(BlendMode::Additive));

const std::array<std::byte, kLitMaterialBytes>& lit_defaults() noexcept {
  static const std::array<std::byte, kLitMaterialBytes> bytes = [] {
    std::array<std::byte, kLitMaterialBytes> out{};
    const LitMaterial defaults{};
    std::array<std::byte, kLitMaterialBytes> image{};
    std::memcpy(image.data(), &defaults, sizeof(defaults));
    // LitMaterial has no array member, so a member's value is its kind's
    // HLSL size from its offset.
    for (const shaders::MemberLayout& member : shaders::BlockLayoutOf<LitMaterial>::value.members) {
      const std::uint32_t size = shaders::kind_hlsl_size(member.kind, member.nested);
      std::memcpy(out.data() + member.offset, image.data() + member.offset, size);
    }
    return out;
  }();
  return bytes;
}

std::optional<shaders::PermutationId> lit_permutation(std::span<const bool> values,
                                                      BlendMode blend) noexcept {
  const LitSwitches switches{.base_color_map = values[0],
                             .normal_map = values[1],
                             .occlusion_reflectance_map = values[2],
                             .masked = blend == BlendMode::Masked};
  if (!lit_switches_admitted(switches)) {
    return std::nullopt;
  }
  return lit_domain(switches).id();
}

std::uint32_t lit_attributes_of(shaders::PermutationId permutation) noexcept {
  return lit_vertex_attributes(lit_switches(LitDomain::from_id(permutation)));
}

} // namespace

const MaterialTemplate& lit_material_template() noexcept {
  static const MaterialTemplate kLit{
      .name = kLitTemplate,
      .shading_model = ShadingModel::Lit,
      .block = &shaders::BlockLayoutOf<LitMaterial>::value,
      .defaults = lit_defaults(),
      .switches = kSwitches,
      .textures = kTextures,
      .sampler = kSamplerMember,
      .blend_modes = kLitBlendModes,
      .permutation = &lit_permutation,
      .vertex_attributes = &lit_attributes_of,
  };
  return kLit;
}

} // namespace bw::materials
