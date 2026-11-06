// Unit test: public contracts of bw/content_converter/material_mapping.hpp;
// each scenario names the declaration it asserts. The default.cpr is built
// here from cpr_container.hpp's framing and the header's description of the
// vfx.texture.create CALL.
#include "bw/content/asset_id.hpp"
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"
#include "bw/content_converter/material_mapping.hpp"
#include "bw/content_converter/mesh_reader.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/testing/harness.hpp"

#include "cpr_fixture.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
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

// The CALL's function name and the slot names of its first and later
// arguments.
struct CallSpec {
  std::string_view function;
  std::string_view first_slot;
  std::string_view later_slot;
};
std::vector<fx::Record> default_cpr(const CallSpec& spec) {
  const auto [function, first_slot, later_slot] = spec;
  std::vector<fx::Argument> arguments{
      {std::string(first_slot), 2, "model.PW0.texture.table.table", 0}};
  for (const std::string_view name : kNames) {
    arguments.push_back({std::string(later_slot), 2, std::string(name), 0});
  }
  return {
      {"USER", "table._tm", "model.pw0.texture.table.table", Bytes(16), Bytes(16), {}, {}},
      {"CALL", std::string(function), "model.pw0.texture.default", {}, {}, arguments, {}},
  };
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

// The runs of decimal digits in `text`, as numbers, in order.
std::vector<std::uint32_t> digit_runs(std::string_view text) {
  std::vector<std::uint32_t> runs;
  std::size_t i = 0;
  while (i < text.size()) {
    if (text[i] < '0' || text[i] > '9') {
      ++i;
      continue;
    }
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data() + i, text.data() + text.size(), value);
    if (error != std::errc{}) {
      return {};
    }
    runs.push_back(value);
    i = static_cast<std::size_t>(end - text.data());
  }
  return runs;
}

cc::ResolvedSlot slot(Tag tag, std::string_view id) {
  const bw::content::AssetIdResult made = bw::content::make_asset_id(id);
  require(made.ok(), "a fixture id validates");
  return {tag, made.id};
}

// (material_mapping.hpp, material_mapping, map_material, kMaterialMapping)
void mapping_rows() {
  using K = cc::MappedKind;
  struct Row {
    std::string_view tag;
    K kind;
    std::string_view parameter;
    std::string_view switch_name;
  };
  const std::array<Row, 15> expected{{
      {"BTX0", K::Texture, m::kBaseColorTexture, ""},
      {"BTX1", K::Texture, m::kSecondBaseColorTexture, m::kHasSecondBaseColor},
      {"BLDT", K::Texture, m::kBlendMaskTexture, m::kHasBlendMask},
      {"NMAP", K::Texture, m::kNormalTexture, m::kHasNormalMap},
      {"AMOC", K::Texture, m::kGlossTexture, m::kHasGloss},
      {"LENV", K::Texture, m::kEnvironmentTexture, m::kHasEnvironment},
      {"LUMX", K::Texture, m::kIlluminanceTexture, m::kHasIlluminanceMap},
      {"CCOL", K::Vector, m::kConstantColor, ""},
      {"FRSR", K::Vector, m::kFresnelR, ""},
      {"FRSA", K::Vector, m::kFresnelA, ""},
      {"LUMI", K::Scalar, m::kIlluminance, ""},
      {"REFP", K::Vector, m::kReflectParameters, ""},
      {"SPCC", K::Vector, m::kSpecularCoefficient, ""},
      {"ENVP", K::Vector, m::kEnvironmentReflectPower, ""},
      {"SSSM", K::Scalar, m::kSubsurfaceLevel, ""},
  }};
  const std::span<const cc::MappingRow> rows = cc::material_mapping();
  require(rows.size() == expected.size(), "one row per mapped field");
  for (std::size_t i = 0; i < expected.size(); ++i) {
    require(rows[i].tag == tag_of(expected[i].tag) && rows[i].kind == expected[i].kind &&
                rows[i].parameter == expected[i].parameter &&
                rows[i].switch_name == expected[i].switch_name,
            i, "the mapping as stated");
  }

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
  block.cf1i[0].value = {0.0F, 0.0F, 0.0F, 0.0F};
  block.cf1i[3].value = {1.0F, 0.0F, 1.0F, 0.0F};
  block.has_sssm = true;
  block.sssm_lenv = {0, 3, 13};
  block.has_srs = true;
  block.srs_tag = tag_of("BTAD");
  block.srs_value = {0, 0, 7};
  const std::array<cc::ResolvedSlot, 5> slots{slot(tag_of("BTX0"), "model.pw0.texture.dds.a"),
                                              slot(tag_of("NMAP"), "model.pw0.texture.dds.b"),
                                              slot(tag_of("ZZZZ"), "model.pw0.texture.dds.c"),
                                              slot(tag_of("BTX0"), "model.pw0.texture.dds.d"),
                                              slot(tag_of("ZZZZ"), "model.pw0.texture.dds.e")};
  const cc::MaterialAssetData data = cc::map_material(block, slots);

  require(data.template_name == m::kLitTemplate, "the lit template");
  const std::vector<std::pair<std::string_view, bool>> switches{
      {m::kHasSecondBaseColor, false}, {m::kHasBlendMask, false},   {m::kHasNormalMap, true},
      {m::kHasGloss, false},           {m::kHasEnvironment, false}, {m::kHasIlluminanceMap, false}};
  require(data.switches.size() == switches.size(), "every has_* switch, present or absent");
  for (std::size_t i = 0; i < switches.size(); ++i) {
    require(data.switches[i].name == switches[i].first &&
                data.switches[i].value == switches[i].second,
            i, "switch value from the slots");
  }
  require(data.textures.size() == 2U && data.textures[0].name == m::kBaseColorTexture &&
              data.textures[0].id == "model.pw0.texture.dds.a" &&
              data.textures[1].name == m::kNormalTexture &&
              data.textures[1].id == "model.pw0.texture.dds.b",
          "mapped slots, the first of a repeated tag");
  const std::vector<std::pair<std::string_view, std::size_t>> vectors{
      {m::kConstantColor, 1},
      {m::kFresnelR, 4},
      {m::kFresnelA, 2},
      {m::kReflectParameters, 6},
      {m::kSpecularCoefficient, 7},
      {m::kEnvironmentReflectPower, 8}};
  require(data.vectors.size() == vectors.size(), "the vector parameters");
  for (std::size_t i = 0; i < vectors.size(); ++i) {
    require(data.vectors[i].name == vectors[i].first &&
                data.vectors[i].value == block.cf1i[vectors[i].second].value,
            i, "vector parameter in mapping order");
  }
  require(data.scalars.size() == 2U && data.scalars[0].name == m::kIlluminance &&
              data.scalars[0].value == 5.0F && data.scalars[1].name == m::kSubsurfaceLevel &&
              data.scalars[1].value == 3.0F,
          "LUMI's r component and SSSM's middle word");
  // The unmapped-field scheme as material_mapping.hpp states it: names and
  // order exactly; CF1I floats shortest round-trip separated by ','; the
  // knsS words in hexadecimal; SRS as its tag, ':', then its three words;
  // TR slots by "TR.<tag>" with the id; a used name gains '.' and the
  // field's position in the list. The header fixes neither the hexadecimal
  // spelling nor the separator or radix of the SRS words, so those are read,
  // not compared as text.
  const std::array<std::string_view, 8> names{"PARF", "FRSB",    "knsS.feature", "knsS.second",
                                              "SRS",  "TR.ZZZZ", "TR.BTX0",      "TR.ZZZZ.7"};
  require(data.unmapped.size() == names.size(), "every unmapped field");
  for (std::size_t i = 0; i < names.size(); ++i) {
    require(data.unmapped[i].name == names[i], i, "unmapped field names, in the stated order");
  }
  require(data.unmapped[0].value == "0,0,0,0" && data.unmapped[1].value == "1,0,1,0",
          "an unmapped CF1I record's four floats, shortest round-trip, separated by ','");
  std::uint32_t feature = 0;
  std::uint32_t second = 0;
  require(hex_value(data.unmapped[2].value, feature) && feature == block.knss_feature &&
              hex_value(data.unmapped[3].value, second) && second == block.knss_second,
          "the knsS words in hexadecimal");
  const std::string_view srs = data.unmapped[4].value;
  require(srs.starts_with("BTAD:") &&
              digit_runs(srs.substr(5)) == std::vector<std::uint32_t>{0, 0, 7},
          "SRS: its tag, ':', then its three words");
  require(data.unmapped[5].value == "model.pw0.texture.dds.c" &&
              data.unmapped[6].value == "model.pw0.texture.dds.d" &&
              data.unmapped[7].value == "model.pw0.texture.dds.e",
          "an unmapped TR slot's id as text; the later of a repeated tag unmapped");
  cc::EncodeFailure failure;
  const std::vector<std::byte> payload = cc::encode_material_asset(data, failure);
  require(failure.ok() && !payload.empty(), "the mapped material encodes: names are unique");
}

constexpr std::array<bw::testing::Scenario<void>, 3> kScenarios{{
    {"table_from_call", table_from_call},
    {"slot_resolution", slot_resolution},
    {"mapping_rows", mapping_rows},
}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
