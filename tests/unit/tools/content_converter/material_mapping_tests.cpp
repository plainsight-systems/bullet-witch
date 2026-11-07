// Unit test: public contracts of bw/content_converter/material_mapping.hpp;
// each scenario names the declaration it asserts. The default.cpr is
// bw/testing/cpr_fixture.hpp's texture_table, built from cpr_container.hpp's
// framing and the header's description of the vfx.texture.create CALL.
#include "bw/content/asset_id.hpp"
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"
#include "bw/content_converter/material_mapping.hpp"
#include "bw/content_converter/mesh_reader.hpp"
#include "bw/content_converter/texture_reader.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/materials/shading.hpp"
#include "bw/testing/cpr_fixture.hpp"
#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace cc = bw::content_converter;
namespace m = bw::materials;
namespace fx = bw::testing::cpr;
using bw::testing::require;
using fx::Bytes;
using Tag = std::array<char, 4>;

Tag tag_of(std::string_view text) { return {text[0], text[1], text[2], text[3]}; }

constexpr std::array<std::string_view, 3> kNames{
    "model.PW0.texture.dds.PW0_body_m1_game_psd",
    "shared_texture.VH0_n1heri_m1_psd.dds.VH0_n1heri_m1_psd",
    "model.PW0.texture.dds.pw0_magazine_psd",
};

std::vector<fx::Record> default_cpr(const fx::TableCall& call) {
  return fx::texture_table(call, kNames);
}

cc::TextureTableError table_of(const std::vector<fx::Record>& records, cc::TextureTable& out,
                               Bytes& file, cc::CprContainer& container) {
  file = fx::build(records);
  require(cc::parse_cpr(file, container) == cc::CprError::None, "the default.cpr parses");
  return cc::texture_table_from_default(container, out);
}

// (material_mapping.hpp, texture_table_from_default, TextureTableError)
void table_from_call() {
  Bytes file;
  cc::CprContainer container;
  cc::TextureTable table;
  require(table_of(default_cpr({cc::kTextureCreateCall, cc::kTableSlot, cc::kDdsSlot}), table, file,
                   container) == cc::TextureTableError::None,
          "the CALL's arguments read");
  require(table.names.size() == kNames.size() &&
              std::equal(table.names.begin(), table.names.end(), kNames.begin()),
          "the dds names in argument order, as the CALL stores them");

  using E = cc::TextureTableError;
  struct Row {
    std::string_view name;
    std::vector<fx::Record> records;
    E expected;
  };
  std::vector<fx::Record> only_table = default_cpr({cc::kTextureCreateCall, "table", "dds"});
  only_table.pop_back();
  const std::array<Row, 2> rows{{
      {"no CALL", only_table, E::CallMissing},
      {"a later slot name", default_cpr({cc::kTextureCreateCall, "table", "dxs"}),
       E::ArgumentSlotUnexpected},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    Bytes f;
    cc::CprContainer c;
    cc::TextureTable t;
    require(table_of(rows[i].records, t, f, c) == rows[i].expected && t.names.empty(), i,
            rows[i].name);
  }
}

struct ListResolver final : cc::RecordResolver {
  std::vector<std::string> names;
  [[nodiscard]] bool exists(std::string_view qualified_name) const override {
    return std::any_of(names.begin(), names.end(), [&](const std::string& n) {
      return cc::equals_ignoring_ascii_case(n, qualified_name);
    });
  }
};

// (material_mapping.hpp, resolve_slot, SlotError order)
void slot_resolution() {
  cc::TextureTable table;
  table.names = {kNames[0], kNames[1], "model.pw0.texture.dds.bad name", "model.pw0.gone.bad name"};
  ListResolver resolver;
  resolver.names = {"model.pw0.texture.dds.pw0_body_m1_game_psd",
                    "SHARED_TEXTURE.vh0_n1heri_m1_psd.dds.vh0_n1heri_m1_psd",
                    "model.pw0.texture.dds.bad name"};
  const cc::SlotResolution first = cc::resolve_slot(table, 0, resolver);
  require(first.error == cc::SlotError::None &&
              first.id.view() == "model.pw0.texture.dds.pw0_body_m1_game_psd",
          "the k-th name, folded to lower case");
  const cc::SlotResolution shared = cc::resolve_slot(table, 1, resolver);
  require(shared.error == cc::SlotError::None &&
              shared.id.view() == "shared_texture.vh0_n1heri_m1_psd.dds.vh0_n1heri_m1_psd",
          "the resolver compares ignoring case");
  struct Row {
    std::uint32_t index;
    cc::SlotError expected;
  };
  const std::array<Row, 3> rows{{
      {4, cc::SlotError::IndexOutOfRange},
      {2, cc::SlotError::IdInvalid},
      // Absent and not an id: the resolver is asked first.
      {3, cc::SlotError::TextureMissing},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const cc::SlotResolution r = cc::resolve_slot(table, rows[i].index, resolver);
    require(r.error == rows[i].expected && r.id.empty(), i, "slot failure");
  }
  ListResolver nothing;
  require(cc::resolve_slot(table, 0, nothing).error == cc::SlotError::TextureMissing,
          "a name that resolves nowhere");
}

// The value of hexadecimal text, with or without a 0x prefix; false when the
// text is not wholly hexadecimal digits.
bool hex_value(std::string_view text, std::uint32_t& value) {
  if (text.starts_with("0x") || text.starts_with("0X")) {
    text.remove_prefix(2);
  }
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 16);
  return error == std::errc{} && end == text.data() + text.size() && !text.empty();
}

cc::ResolvedSlot slot(Tag tag, std::string_view id) {
  const bw::content::AssetIdResult made = bw::content::make_asset_id(id);
  require(made.ok(), "a fixture id validates");
  return {tag, made.id};
}

// A block with the content's nine CF1I records in their order, record k
// holding (k, k + 0.5, k + 0.25, 1); SPCC is (n, k) = (40, 0.1).
cc::MaterialBlock fixture_block() {
  cc::MaterialBlock block{};
  block.knss_feature = 0x00022490U;
  block.knss_second = 0x167E0001U;
  const std::array<std::string_view, 9> tags{"PARF", "CCOL", "FRSA", "FRSB", "FRSR",
                                             "LUMI", "REFP", "SPCC", "ENVP"};
  for (std::size_t k = 0; k < tags.size(); ++k) {
    block.cf1i[k].tag = tag_of(tags[k]);
    const auto f = static_cast<float>(k);
    block.cf1i[k].value = {f, f + 0.5F, f + 0.25F, 1.0F};
  }
  block.cf1i[7].value = {40.0F, 0.1F, 0.0F, 0.0F};
  return block;
}

// (material_mapping.hpp, map_material and the mapping onto the lit
// template): row by row from a fixture block. Slots:
// the three lit slots to texture and switch, the first of a repeated tag;
// BTX1 to the switch base_color_1 = 1 and its id kept; an unknown tag kept.
// CF1I: CCOL as stored, LUMI r as (r, r, r, 0); SPCC, REFP x and an authored
// FRSR through highlight_reflectance to the reflectance curve; the tint, the
// base brightness weight and the opacity curve clamped; threshold 0.5; the
// tags kept whole, then the rest unmapped in the stated order. Then
// highlight_reflectance at two exponents, and a block with no highlight
// keeping the template's defaults.
void lit_mapping() {
  cc::MaterialBlock block = fixture_block();
  block.has_sssm = true;
  block.sssm_lenv = {0, 3, 13};
  block.cf1i[2].value = {3.0F, 0.9F, 0.5F, 0.0F};    // FRSA
  block.cf1i[4].value = {5.0F, 0.75F, 0.2F, 0.0F};   // FRSR, authored
  block.cf1i[6].value = {0.15F, 2.0F, -0.5F, 0.25F}; // REFP
  const std::array<cc::ResolvedSlot, 6> slots{slot(tag_of("BTX0"), "model.pw0.texture.dds.a"),
                                              slot(tag_of("AMOC"), "model.pw0.texture.dds.g"),
                                              slot(tag_of("BTX1"), "model.pw0.texture.dds.b"),
                                              slot(tag_of("ZZZZ"), "model.pw0.texture.dds.c"),
                                              slot(tag_of("BTX0"), "model.pw0.texture.dds.d"),
                                              slot(tag_of("NMAP"), "model.pw0.texture.dds.n")};
  const cc::MaterialMapping mapped = cc::map_material(block, slots, cc::BaseAlpha::Opaque);
  require(mapped.ok(), "the block maps");
  const cc::MaterialAssetData& data = mapped.asset;
  require(data.template_name == m::kLitTemplate, "the lit template");

  const std::array<std::pair<std::string_view, bool>, 4> switches{
      {{m::kBaseColorMapSwitch, true},
       {m::kNormalMapSwitch, true},
       {m::kOcclusionReflectanceMapSwitch, true},
       {cc::kSecondBaseColorSwitch, true}}};
  require(data.switches.size() == switches.size(), "the three lit switches, then base_color_1");
  for (std::size_t i = 0; i < switches.size(); ++i) {
    require(data.switches[i].name == switches[i].first &&
                data.switches[i].value == switches[i].second,
            i, "switch in record order");
  }
  const std::array<std::pair<std::string_view, std::string_view>, 3> textures{
      {{m::kBaseColorTexture, "model.pw0.texture.dds.a"},
       {m::kNormalTexture, "model.pw0.texture.dds.n"},
       {m::kOcclusionReflectanceTexture, "model.pw0.texture.dds.g"}}};
  require(data.textures.size() == textures.size(), "the three lit textures");
  for (std::size_t i = 0; i < textures.size(); ++i) {
    require(data.textures[i].name == textures[i].first && data.textures[i].id == textures[i].second,
            i, "texture in slot order, the first of a repeated tag");
  }
  const float w = cc::highlight_reflectance(40.0F, 0.1F, 0.15F);
  const std::array<std::pair<std::string_view, float>, 11> scalars{
      {{m::kBlendModeParameter, 0.0F},
       {m::kRoughness, cc::roughness_from_exponent(40.0F)},
       {m::kReflectance, w * 0.2F},
       {m::kGrazingReflectance, w * (0.75F + 0.2F)},
       {m::kFresnelExponent, 5.0F},
       {m::kSpecularTint, 1.0F},
       {m::kReflectanceBaseLuminance, 0.0F},
       {m::kOpacityFacing, 0.5F},
       {m::kOpacityGrazing, 1.0F},
       {m::kOpacityFresnelExponent, 3.0F},
       {m::kOpacityMaskThreshold, 0.5F}}};
  require(data.scalars.size() == scalars.size(), "the eleven scalars");
  for (std::size_t i = 0; i < scalars.size(); ++i) {
    require(data.scalars[i].name == scalars[i].first && data.scalars[i].value == scalars[i].second,
            i, "scalar in record order, the curve's ends saturated, the weights clamped");
  }
  require(std::abs(cc::roughness_from_exponent(40.0F) - 0.46714F) < 1e-4F &&
              cc::roughness_from_exponent(0.0F) == 1.0F,
          "(2 / (n + 2))^0.25, 1 at n 0");
  require(data.vectors.size() == 2U && data.vectors[0].name == m::kBaseColorTint &&
              data.vectors[0].value == block.cf1i[1].value &&
              data.vectors[1].name == m::kEmissive &&
              data.vectors[1].value == std::array{5.0F, 5.0F, 5.0F, 0.0F},
          "CCOL as stored, LUMI's r as (r, r, r, 0)");

  const std::array<std::string_view, 12> names{
      "FRSR", "FRSA",       "REFP",         "ENVP",        "SPCC",    "PARF",
      "FRSB", "SSSM.level", "knsS.feature", "knsS.second", "TR.BTX1", "TR.ZZZZ"};
  const std::vector<cc::UnmappedField>& unmapped = mapped.unmapped;
  require(unmapped.size() == names.size() + 1U, "every unmapped field");
  for (std::size_t i = 0; i < names.size(); ++i) {
    require(unmapped[i].name == names[i], i, "unmapped field names, in the stated order");
  }
  require(unmapped[12].name == "TR.BTX0" && unmapped[12].value == "model.pw0.texture.dds.d",
          "the later of a repeated tag unmapped");
  require(unmapped[0].value == "5,0.75,0.2,0" && unmapped[4].value == "40,0.1,0,0" &&
              unmapped[5].value == "0,0.5,0.25,1" && unmapped[7].value == "3",
          "a CF1I record's floats separated by ',', mapped tags whole; SSSM's middle word in "
          "decimal");
  std::uint32_t feature = 0;
  require(hex_value(unmapped[8].value, feature) && feature == block.knss_feature &&
              unmapped[10].value == "model.pw0.texture.dds.b",
          "the knsS word in hexadecimal; a slot's id as text");

  struct Highlight {
    float exponent;
    float expected;
  };
  // 100 * 8 * (2^(-n / 2) + n) / ((n + 2) * (n + 4)) at k = 1, reflectivity 1.
  const std::array<Highlight, 2> highlights{{{1.0F, 91.045695F}, {100.0F, 7.5414781F}}};
  for (std::size_t i = 0; i < highlights.size(); ++i) {
    const float got = cc::highlight_reflectance(highlights[i].exponent, 1.0F, 1.0F);
    require(std::abs(got - highlights[i].expected) <= 1e-5F * highlights[i].expected, i,
            "highlight_reflectance at an exponent");
  }
  require(cc::highlight_reflectance(40.0F, 0.5F, 0.5F) ==
                  0.25F * cc::highlight_reflectance(40.0F, 1.0F, 1.0F) &&
              cc::highlight_reflectance(0.0F, 0.5F, 1.0F) == 0.0F &&
              cc::highlight_reflectance(40.0F, 0.0F, 1.0F) == 0.0F,
          "linear in k and reflectivity; 0 at n 0 and at k 0");

  cc::MaterialBlock dull = fixture_block();
  dull.cf1i[7].value = {40.0F, 0.0F, 0.0F, 0.0F};
  const cc::MaterialMapping dull_mapped = cc::map_material(dull, {}, cc::BaseAlpha::NoTexture);
  const auto named = [&dull_mapped](std::string_view name) {
    return std::any_of(dull_mapped.asset.scalars.begin(), dull_mapped.asset.scalars.end(),
                       [name](const cc::NamedScalar& s) { return s.name == name; });
  };
  require(dull_mapped.ok() && dull_mapped.asset.scalars[1].name == m::kRoughness &&
              dull_mapped.asset.scalars[1].value == 1.0F && !named(m::kReflectance) &&
              !named(m::kGrazingReflectance) && !named(m::kFresnelExponent) &&
              named(m::kSpecularTint),
          "no highlight: roughness 1 and no reflectance scalar, so the defaults apply");
}

// (material_mapping.hpp, render_unmapped_fields, parse_unmapped_fields):
// what render writes parses back to the same fields, an empty value
// included; text with no final line end, a line with no tab and an empty
// name are each refused, leaving the output empty.
void unmapped_fields_file() {
  const std::vector<cc::UnmappedField> fields{
      {"FRSR", "1,0,1,0"}, {"knsS.feature", "0x00022490"}, {"TR.LENV", ""}};
  const std::string text = cc::render_unmapped_fields(fields);
  std::vector<cc::UnmappedField> back;
  require(text == "FRSR\t1,0,1,0\nknsS.feature\t0x00022490\nTR.LENV\t\n" &&
              cc::parse_unmapped_fields(text, back) && back.size() == fields.size(),
          "render then parse");
  for (std::size_t i = 0; i < fields.size(); ++i) {
    require(back[i].name == fields[i].name && back[i].value == fields[i].value, i,
            "the same field");
  }
  const std::array<std::pair<std::string_view, std::string_view>, 3> refused{{
      {"no final line end", "FRSR\t1,0,1,0"},
      {"a line with no tab", "FRSR\t1,0,1,0\nknsS.feature\n"},
      {"an empty name", "\t1,0,1,0\n"},
  }};
  for (std::size_t i = 0; i < refused.size(); ++i) {
    std::vector<cc::UnmappedField> out{{"stale", "stale"}};
    require(!cc::parse_unmapped_fields(refused[i].second, out) && out.empty(), i, refused[i].first);
  }
}

// One image of the given alpha values, white.
cc::Rgba8Image alpha_image(std::initializer_list<std::uint8_t> alphas) {
  cc::Rgba8Image image;
  image.width = static_cast<std::uint32_t>(alphas.size());
  image.height = 1;
  for (const std::uint8_t a : alphas) {
    image.pixels.insert(image.pixels.end(), {255, 255, 255, a});
  }
  return image;
}

// (material_mapping.hpp, classify_base_alpha, blend_mode_of): the three
// classes, with the graded boundary one step from each end; then every
// cell of four alpha classes by three markers, and map_material carrying
// the blend mode as its scalar and marking the graded unmarked base.
void blend_rule() {
  require(cc::classify_base_alpha(alpha_image({255, 255})) == cc::BaseAlpha::Opaque &&
              cc::classify_base_alpha(alpha_image({0, 255})) == cc::BaseAlpha::Binary &&
              cc::classify_base_alpha(alpha_image({0, 255, 254})) == cc::BaseAlpha::Graded &&
              cc::classify_base_alpha(alpha_image({1, 255})) == cc::BaseAlpha::Graded,
          "opaque, binary, and graded at 254 and at 1");
  using B = m::BlendMode;
  using A = cc::BaseAlpha;
  constexpr std::array<A, 4> classes{A::NoTexture, A::Opaque, A::Binary, A::Graded};
  // Per marker (none, BTNO, BTAD), the blend mode of each class.
  const std::array<std::pair<std::string_view, std::array<B, 4>>, 3> markers{{
      {"", {B::Opaque, B::Opaque, B::Masked, B::Opaque}},
      {"BTNO", {B::Translucent, B::Translucent, B::Translucent, B::Translucent}},
      {"BTAD", {B::Additive, B::Additive, B::Additive, B::Additive}},
  }};
  for (std::size_t k = 0; k < markers.size(); ++k) {
    cc::MaterialBlock block = fixture_block();
    block.has_srs = !markers[k].first.empty();
    if (block.has_srs) {
      block.srs_tag = tag_of(markers[k].first);
    }
    for (std::size_t c = 0; c < classes.size(); ++c) {
      const std::size_t row = (k * classes.size()) + c;
      B mode = B::Modulate;
      require(cc::blend_mode_of(block, classes[c], mode) == cc::MaterialMappingError::None &&
                  mode == markers[k].second[c],
              row, "the blend mode of the cell");
    }
  }
  // The cell where map_material adds to blend_mode_of: an unmarked graded
  // base keeps Opaque and is marked graded.
  const std::array<cc::ResolvedSlot, 1> base{slot(tag_of("BTX0"), "model.pw0.texture.dds.a")};
  const cc::MaterialMapping mapped = cc::map_material(fixture_block(), base, A::Graded);
  const auto graded =
      std::find_if(mapped.unmapped.begin(), mapped.unmapped.end(),
                   [](const cc::UnmappedField& f) { return f.name == "base_alpha"; });
  require(mapped.ok() && mapped.asset.scalars[0].name == m::kBlendModeParameter &&
              mapped.asset.scalars[0].value == static_cast<float>(static_cast<int>(B::Opaque)) &&
              graded != mapped.unmapped.end() && graded->value == "graded",
          "the scalar blend_mode, and base_alpha graded for an unmarked graded base");
}

// (material_mapping.hpp, map_material): one row per error name; the asset
// and the unmapped fields are empty on a failure.
void refusals_named() {
  struct Row {
    std::string_view note;
    std::string_view srs;
    std::string_view slot;
    cc::MaterialMappingError error;
    std::string_view name;
  };
  const std::array<Row, 2> rows{{
      {"an occlusion and reflectance map without a base color map", "", "AMOC",
       cc::MaterialMappingError::SwitchesNotAdmitted, m::kOcclusionReflectanceMapSwitch},
      {"an SRS tag that is no marker", "BTXX", "BTX0", cc::MaterialMappingError::BlendMarkerUnknown,
       "BTXX"},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    cc::MaterialBlock block = fixture_block();
    block.has_srs = !rows[i].srs.empty();
    if (block.has_srs) {
      block.srs_tag = tag_of(rows[i].srs);
    }
    const std::array<cc::ResolvedSlot, 1> slots{
        slot(tag_of(rows[i].slot), "model.pw0.texture.dds.a")};
    const cc::MaterialMapping mapped = cc::map_material(block, slots, cc::BaseAlpha::Opaque);
    require(mapped.error == rows[i].error && mapped.name == rows[i].name &&
                mapped.asset.template_name.empty() && mapped.asset.switches.empty() &&
                mapped.unmapped.empty(),
            i, rows[i].note);
  }
}

constexpr std::array<bw::testing::Scenario<void>, 6> kScenarios{{
    {"table_from_call", table_from_call},
    {"slot_resolution", slot_resolution},
    {"lit_mapping", lit_mapping},
    {"blend_rule", blend_rule},
    {"refusals_named", refusals_named},
    {"unmapped_fields_file", unmapped_fields_file},
}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
