// Unit test: public contracts of bw/rendering/materials/material.hpp over the
// lit template (lit_template.hpp). Payloads are built by material_payload.hpp.
#include "bw/rendering/materials/lit_template.hpp"
#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/materials/shading.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/testing/harness.hpp"
#include "bw/testing/material_payload.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

namespace m = bw::materials;
namespace mp = bw::testing::materials;
using bw::testing::require;

constexpr std::string_view kBaseId = "model.test.texture.base";
constexpr bw::shaders::TextureIndex kBaseIndex{7};
constexpr bw::shaders::SamplerIndex kSampler{3};

// A masked lit material with a base color map: tint, emissive (its fourth
// component is not a member's), roughness and the base color texture.
mp::PayloadSpec masked_spec() {
  return {.switches = {{m::kBaseColorMapSwitch, 1},
                       {m::kNormalMapSwitch, 0},
                       {m::kOcclusionReflectanceMapSwitch, 0}},
          .scalars = {{m::kBlendModeParameter, 1.0F}, {m::kRoughness, 0.25F}},
          .vectors = {{m::kBaseColorTint, {0.5F, 0.25F, 0.125F, 1.0F}},
                      {m::kEmissive, {1.0F, 2.0F, 3.0F, 9.0F}}},
          .textures = {{m::kBaseColorTexture, kBaseId}}};
}

struct Created {
  m::Material material;
  m::MaterialFailure failure;
};

Created create(const mp::PayloadSpec& spec, std::span<const m::MaterialTextureBinding> textures) {
  const mp::Bytes payload = mp::build_payload(spec);
  m::MaterialAssetView view;
  require(m::decode_material_asset(payload, view).error == m::MaterialAssetError::None,
          "the payload decodes");
  Created out;
  out.failure = m::create_material(view, textures, kSampler, out.material);
  return out;
}

constexpr std::array kBound{m::MaterialTextureBinding{m::kBaseColorTexture, kBaseIndex}};

m::LitMaterial block_of(std::span<const std::byte> bytes) {
  require(bytes.size() == sizeof(m::LitMaterial), "the block is LitMaterial's size");
  m::LitMaterial block{};
  std::memcpy(&block, bytes.data(), sizeof(block));
  return block;
}

// (material.hpp, contract 3; lit_template.hpp, contracts 3 and 6): the lit
// template's shading model; a masked asset gives the masked
// permutation of its switches, each parameter at its member, the defaults
// elsewhere, the texture and sampler indices bound and zero outside every
// member; the same asset with no blend_mode scalar is Opaque and not masked.
void created_from_asset() {
  const m::MaterialTemplate& lit = m::lit_material_template();
  require(lit.shading_model == m::ShadingModel::Lit, "the lit template's shading model");

  const Created masked = create(masked_spec(), kBound);
  require(masked.failure.ok(), "the masked asset creates");
  require(masked.material.material_template() == &lit &&
              masked.material.blend_mode() == m::BlendMode::Masked &&
              masked.material.permutation() ==
                  m::lit_domain({.base_color_map = true, .masked = true}).id(),
          "template, blend mode and the masked permutation");
  const m::LitMaterial block = block_of(masked.material.block());
  require(block.base_color_tint.x == 0.5F && block.base_color_tint.y == 0.25F &&
              block.base_color_tint.z == 0.125F && block.base_color_tint.w == 1.0F &&
              block.emissive.x == 1.0F && block.emissive.y == 2.0F && block.emissive.z == 3.0F &&
              block.roughness == 0.25F,
          "each parameter at its member's offset; a Float3 from the first three components");
  require(block.reflectance == 0.04F && block.grazing_reflectance == 1.0F &&
              block.fresnel_exponent == 5.0F && block.specular_tint == 0.0F &&
              block.reflectance_base_luminance == 0.0F && block.opacity_mask_threshold == 0.5F &&
              block.opacity_facing == 1.0F && block.opacity_grazing == 1.0F &&
              block.opacity_fresnel_exponent == 1.0F && block.normal.value == 0U &&
              block.occlusion_reflectance.value == 0U,
          "an omitted parameter keeps its default");
  require(block.base_color.value == kBaseIndex.value && block.sampler.value == kSampler.value,
          "the texture and sampler indices");
  std::uint32_t outside = 1;
  std::memcpy(&outside, masked.material.block().data() + 28, sizeof(outside));
  require(outside == 0U, "the byte after emissive's z is outside every member and zero");

  mp::PayloadSpec opaque = masked_spec();
  opaque.scalars.erase(opaque.scalars.begin());
  const Created plain = create(opaque, kBound);
  require(plain.failure.ok() && plain.material.blend_mode() == m::BlendMode::Opaque &&
              plain.material.permutation() == m::lit_domain({.base_color_map = true}).id(),
          "no blend_mode scalar is Opaque, and the masked dimension follows it");
}

// (material.hpp, contract 4): one row per error name, each breaking one
// rule of an asset that creates, with the name concerned; `out` is empty.
void mismatches_named() {
  using E = m::MaterialError;
  struct Row {
    std::string_view note;
    std::function<void(mp::PayloadSpec&, std::vector<m::MaterialTextureBinding>&)> change;
    E error;
    // The name the failure carries; nullopt where only the error is asserted.
    std::optional<std::string_view> name;
  };
  using Bindings = std::vector<m::MaterialTextureBinding>;
  const std::array<Row, 9> rows{{
      {"no such template", [](mp::PayloadSpec& s, Bindings&) { s.template_name = "glass"; },
       E::TemplateUnknown, "glass"},
      {"a switch no template declares",
       [](mp::PayloadSpec& s, Bindings&) { s.switches.emplace_back("environment_map", 1); },
       E::SwitchUnknown, "environment_map"},
      {"a declared switch absent", [](mp::PayloadSpec& s, Bindings&) { s.switches.pop_back(); },
       E::SwitchMissing, m::kOcclusionReflectanceMapSwitch},
      {"modulate is not drawn", [](mp::PayloadSpec& s, Bindings&) { s.scalars[0].second = 4.0F; },
       E::BlendModeUnsupported, std::nullopt},
      {"an occlusion and reflectance map without a base color map",
       [](mp::PayloadSpec& s, Bindings&) {
         s.switches = {{m::kBaseColorMapSwitch, 0},
                       {m::kNormalMapSwitch, 0},
                       {m::kOcclusionReflectanceMapSwitch, 1}};
         s.scalars[0].second = 0.0F;
         s.textures = {{m::kOcclusionReflectanceTexture, kBaseId}};
       },
       E::PermutationNotAdmitted, ""},
      {"a scalar no member has",
       [](mp::PayloadSpec& s, Bindings&) { s.scalars.emplace_back("shininess", 1.0F); },
       E::ParameterUnknown, "shininess"},
      {"a set switch without its texture",
       [](mp::PayloadSpec& s, Bindings&) { s.textures.clear(); }, E::TextureMissing,
       m::kBaseColorTexture},
      {"a texture whose switch is clear",
       [](mp::PayloadSpec& s, Bindings& b) {
         s.textures.emplace_back(m::kNormalTexture, kBaseId);
         b.push_back({m::kNormalTexture, kBaseIndex});
       },
       E::TextureUnused, m::kNormalTexture},
      {"a texture the caller did not resolve", [](mp::PayloadSpec&, Bindings& b) { b.clear(); },
       E::TextureUnbound, m::kBaseColorTexture},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    mp::PayloadSpec spec = masked_spec();
    Bindings bindings(kBound.begin(), kBound.end());
    rows[i].change(spec, bindings);
    const Created made = create(spec, bindings);
    const std::optional<std::string_view> name = rows[i].name;
    const bool named = !name.has_value() || made.failure.name == *name;
    require(made.failure.error == rows[i].error && named &&
                made.material.material_template() == nullptr && made.material.block().empty(),
            i, rows[i].note);
  }
}

// (material.hpp, contract 6): an instance equals its material; each set
// changes its member's bytes and no other, and never the permutation or the
// blend mode; a failed set changes nothing.
void instance_overrides() {
  const Created made = create(masked_spec(), kBound);
  require(made.failure.ok(), "the material creates");
  m::MaterialInstance instance(made.material);
  const auto snapshot = [&instance] {
    return std::vector<std::byte>(instance.block().begin(), instance.block().end());
  };
  require(snapshot() == std::vector<std::byte>(made.material.block().begin(),
                                               made.material.block().end()) &&
              instance.material_template() == made.material.material_template() &&
              instance.permutation() == made.material.permutation() &&
              instance.blend_mode() == made.material.blend_mode(),
          "an instance starts as its material");

  struct Row {
    std::string_view note;
    std::function<m::MaterialError()> set;
    m::MaterialError error;
    // The bytes [first, last) the set may change; empty for a failure.
    std::size_t first;
    std::size_t last;
  };
  const std::array<Row, 5> rows{{
      {"roughness", [&] { return instance.set_scalar(m::kRoughness, 0.75F); },
       m::MaterialError::None, offsetof(m::LitMaterial, roughness),
       offsetof(m::LitMaterial, roughness) + 4},
      {"emissive, three words", [&] { return instance.set_vector(m::kEmissive, {4, 5, 6, 7}); },
       m::MaterialError::None, offsetof(m::LitMaterial, emissive),
       offsetof(m::LitMaterial, emissive) + 12},
      {"base color", [&] { return instance.set_texture(m::kBaseColorTexture, {11}); },
       m::MaterialError::None, offsetof(m::LitMaterial, base_color),
       offsetof(m::LitMaterial, base_color) + 4},
      {"a vector name as a scalar", [&] { return instance.set_scalar(m::kBaseColorTint, 1.0F); },
       m::MaterialError::ParameterUnknown, 0, 0},
      {"a texture whose switch is clear",
       [&] { return instance.set_texture(m::kNormalTexture, {12}); },
       m::MaterialError::TextureUnused, 0, 0},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const std::vector<std::byte> before = snapshot();
    require(rows[i].set() == rows[i].error, i, rows[i].note);
    const std::vector<std::byte> after = snapshot();
    for (std::size_t b = 0; b < before.size(); ++b) {
      require(before[b] == after[b] || (b >= rows[i].first && b < rows[i].last), i,
              "a byte outside the member changed");
    }
    require(rows[i].first == rows[i].last || before != after, i, "the member changed");
    require(instance.permutation() == made.material.permutation() &&
                instance.blend_mode() == made.material.blend_mode(),
            i, "the permutation and blend mode stay");
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"created_from_asset", &created_from_asset},
    Scenario{"mismatches_named", &mismatches_named},
    Scenario{"instance_overrides", &instance_overrides},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
