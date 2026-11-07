// Unit test: public contracts of bw/rendering/materials/material_asset.hpp;
// each scenario names the declaration it asserts. Payloads are built by
// material_payload.hpp, which copies the header's records to the 16-aligned
// offsets it states.
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/testing/harness.hpp"
#include "bw/testing/material_payload.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string_view>
#include <utility>

namespace {

namespace m = bw::materials;
using bw::testing::require;
using bw::testing::materials::build_payload;
using bw::testing::materials::Bytes;
using bw::testing::materials::write_at;
using E = m::MaterialAssetError;

template <class T> T read_at(const Bytes& bytes, std::size_t offset) {
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

constexpr std::string_view kBodyId = "model.pw0.texture.dds.pw0_body_m1_game_psd";
constexpr std::string_view kNormalId = "model.pw0.texture.dds.pw0_body_n1_game_psd";

// Template "lit"; two switches (normal_map 1, occlusion_reflectance_map 0),
// two scalars, one vector and two textures.
Bytes valid_payload() {
  return build_payload(
      {.switches = {{m::kNormalMapSwitch, 1}, {m::kOcclusionReflectanceMapSwitch, 0}},
       .scalars = {{m::kReflectance, 0.5F}, {m::kRoughness, 0.25F}},
       .vectors = {{m::kBaseColorTint, {1.0F, 0.5F, 0.25F, 1.0F}}},
       .textures = {{m::kBaseColorTexture, kBodyId}, {m::kNormalTexture, kNormalId}}});
}

m::MaterialPayloadHeader header_of(const Bytes& b) {
  return read_at<m::MaterialPayloadHeader>(b, 0);
}

void patch_header(Bytes& b, const std::function<void(m::MaterialPayloadHeader&)>& change) {
  auto h = header_of(b);
  change(h);
  write_at(b, 0, h);
}
// Record `index` of the table at `table_offset` of the header.
template <class T>
void patch_record(Bytes& b, std::uint32_t m::MaterialPayloadHeader::* table_offset,
                  std::uint32_t index, const std::function<void(T&)>& change) {
  const std::size_t offset = header_of(b).*table_offset + (std::size_t{index} * sizeof(T));
  auto r = read_at<T>(b, offset);
  change(r);
  write_at(b, offset, r);
}

// (material_asset.hpp, decode_material_asset, MaterialAssetError,
// MaterialAssetView): a valid payload reads back its template name, each
// find_* hit with its value and each miss, and string() over a record's
// slice; each row then breaks one rule and names its failure with its table
// and record: a header failure table 0, a section failure its section's
// table (4 the string table) and the template name table 4, each with
// record 0 (MaterialAssetFailure).
void rejections_named() {
  const Bytes valid = valid_payload();
  {
    m::MaterialAssetView view;
    require(m::decode_material_asset(valid, view).error == E::None, "the valid payload decodes");
    require(view.template_name() == m::kLitTemplate, "template name reads back");
    const auto base = view.find_texture(m::kBaseColorTexture);
    const auto normal = view.find_texture(m::kNormalTexture);
    require(base.found && base.id == kBodyId && normal.found && normal.id == kNormalId,
            "find_texture hits return the ids");
    const auto reflectance = view.find_scalar(m::kReflectance);
    const auto roughness = view.find_scalar(m::kRoughness);
    require(reflectance.found && reflectance.value == 0.5F && roughness.found &&
                roughness.value == 0.25F,
            "find_scalar hits return the values");
    const auto color = view.find_vector(m::kBaseColorTint);
    require(color.found && color.value == std::array{1.0F, 0.5F, 0.25F, 1.0F},
            "find_vector hit returns the value");
    const auto has_normal = view.find_switch(m::kNormalMapSwitch);
    const auto has_packed = view.find_switch(m::kOcclusionReflectanceMapSwitch);
    require(has_normal.found && has_normal.value && has_packed.found && !has_packed.value,
            "find_switch hits return 1 as true and 0 as false");
    require(!view.find_texture(m::kOcclusionReflectanceTexture).found &&
                !view.find_scalar(m::kFresnelExponent).found &&
                !view.find_vector(m::kEmissive).found && !view.find_switch("environment_map").found,
            "each lookup misses a name its table lacks");
    require(!view.find_scalar(m::kBaseColorTint).found,
            "a lookup reads its own table only: a vector name is no scalar");
    const m::ScalarRecord first = view.scalar_record(0);
    require(view.string(first.name_offset, first.name_length) == m::kReflectance,
            "string() reads a record's name");
  }

  using H = m::MaterialPayloadHeader;
  struct Row {
    std::string_view note;
    std::function<void(Bytes&)> change;
    E expected;
    std::uint32_t table;
    std::uint32_t index;
  };
  const std::array rows{
      Row{"header one byte short", [](Bytes& b) { b.resize(sizeof(H) - 1U); }, E::HeaderTruncated,
          0, 0},
      Row{"switch table off 16",
          [](Bytes& b) { patch_header(b, [](H& h) { h.switches_offset += 4U; }); },
          E::SectionMisaligned, 0, 0},
      Row{"texture table past the end",
          [](Bytes& b) { patch_header(b, [](H& h) { h.texture_count = 0xFFFFFFFFU; }); },
          E::SectionOutOfBounds, 3, 0},
      Row{"empty template name",
          [](Bytes& b) { patch_header(b, [](H& h) { h.template_name_length = 0; }); },
          E::TemplateNameInvalid, 4, 0},
      Row{"an empty scalar name",
          [](Bytes& b) {
            patch_record<m::ScalarRecord>(b, &H::scalars_offset, 1,
                                          [](m::ScalarRecord& r) { r.name_length = 0; });
          },
          E::NameInvalid, 1, 1},
      Row{"a scalar named twice",
          [](Bytes& b) {
            const auto first = read_at<m::ScalarRecord>(b, header_of(b).scalars_offset);
            patch_record<m::ScalarRecord>(b, &H::scalars_offset, 1, [&](m::ScalarRecord& r) {
              r.name_offset = first.name_offset;
              r.name_length = first.name_length;
            });
          },
          E::NameDuplicate, 1, 1},
      Row{"a switch value of 2",
          [](Bytes& b) {
            patch_record<m::SwitchRecord>(b, &H::switches_offset, 1,
                                          [](m::SwitchRecord& r) { r.value = 2; });
          },
          E::SwitchValueInvalid, 0, 1},
      Row{"a texture id that does not validate (empty)",
          [](Bytes& b) {
            patch_record<m::TextureRecord>(b, &H::textures_offset, 1,
                                           [](m::TextureRecord& r) { r.id_length = 0; });
          },
          E::TextureIdInvalid, 3, 1},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    Bytes bytes = valid;
    rows[i].change(bytes);
    m::MaterialAssetView view;
    const auto failure = m::decode_material_asset(bytes, view);
    require(failure.error == rows[i].expected && failure.table == rows[i].table &&
                failure.index == rows[i].index,
            i, rows[i].note);
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"rejections_named", &rejections_named},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
