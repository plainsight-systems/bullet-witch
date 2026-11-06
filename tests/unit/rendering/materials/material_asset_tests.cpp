// Unit test: public contracts of bw/rendering/materials/material_asset.hpp;
// each scenario names the declaration it asserts. Payloads are built here by
// copying the header's records to the 16-aligned offsets it states.
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace m = bw::materials;
using bw::testing::require;
using E = m::MaterialAssetError;

using Bytes = std::vector<std::byte>;

void pad16(Bytes& bytes) { bytes.resize((bytes.size() + 15U) & ~std::size_t{15}); }

template <class T> T read_at(const Bytes& bytes, std::size_t offset) {
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}
template <class T> void write_at(Bytes& bytes, std::size_t offset, const T& value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(T));
}
template <class T> std::uint32_t append_records(Bytes& bytes, const std::vector<T>& records) {
  pad16(bytes);
  const auto offset = static_cast<std::uint32_t>(bytes.size());
  for (const T& r : records) {
    bytes.resize(bytes.size() + sizeof(T));
    write_at(bytes, bytes.size() - sizeof(T), r);
  }
  return offset;
}

struct Strings {
  std::string text;
  // Appends `s` and returns its slice as (offset, length).
  std::pair<std::uint32_t, std::uint32_t> add(std::string_view s) {
    const auto offset = static_cast<std::uint32_t>(text.size());
    text += s;
    return {offset, static_cast<std::uint32_t>(s.size())};
  }
};

constexpr std::string_view kBodyId = "model.pw0.texture.dds.pw0_body_m1_game_psd";
constexpr std::string_view kNormalId = "model.pw0.texture.dds.pw0_body_n1_game_psd";
constexpr std::string_view kUnmappedName = "CF1I.word7";

// Template "lit"; two switches (has_normal_map 1, has_gloss 0), two scalars,
// one vector, two textures and two unmapped fields, the second with an empty
// value. Sections in the header's order, each at the next 16-byte boundary.
Bytes valid_payload() {
  Strings s;
  m::MaterialPayloadHeader h{};
  std::tie(h.template_name_offset, h.template_name_length) = s.add(m::kLitTemplate);
  const auto named = [&](std::string_view name, auto record) {
    std::tie(record.name_offset, record.name_length) = s.add(name);
    return record;
  };
  const std::vector switches{named(m::kHasNormalMap, m::SwitchRecord{0, 0, 1, 0}),
                             named(m::kHasGloss, m::SwitchRecord{0, 0, 0, 0})};
  const std::vector scalars{named(m::kSpecularCoefficient, m::ScalarRecord{0, 0, 0.5F, 0}),
                            named(m::kFresnelR, m::ScalarRecord{0, 0, 0.25F, 0})};
  const std::vector vectors{
      named(m::kConstantColor, m::VectorRecord{0, 0, {1.0F, 0.5F, 0.25F, 1.0F}, 0, 0})};
  std::vector<m::TextureRecord> textures;
  for (const auto& [name, id] :
       {std::pair{m::kBaseColorTexture, kBodyId}, std::pair{m::kNormalTexture, kNormalId}}) {
    m::TextureRecord r = named(name, m::TextureRecord{});
    std::tie(r.id_offset, r.id_length) = s.add(id);
    textures.push_back(r);
  }
  std::vector<m::UnmappedFieldRecord> unmapped;
  for (const auto& [name, value] : {std::pair{kUnmappedName, std::string_view{"0x3f800000"}},
                                    std::pair{std::string_view{"TR.flags"}, std::string_view{}}}) {
    m::UnmappedFieldRecord r = named(name, m::UnmappedFieldRecord{});
    std::tie(r.value_offset, r.value_length) = s.add(value);
    unmapped.push_back(r);
  }
  h.switch_count = static_cast<std::uint32_t>(switches.size());
  h.scalar_count = static_cast<std::uint32_t>(scalars.size());
  h.vector_count = static_cast<std::uint32_t>(vectors.size());
  h.texture_count = static_cast<std::uint32_t>(textures.size());
  h.unmapped_count = static_cast<std::uint32_t>(unmapped.size());
  h.string_bytes = static_cast<std::uint32_t>(s.text.size());
  Bytes bytes(sizeof(m::MaterialPayloadHeader));
  h.switches_offset = append_records(bytes, switches);
  h.scalars_offset = append_records(bytes, scalars);
  h.vectors_offset = append_records(bytes, vectors);
  h.textures_offset = append_records(bytes, textures);
  h.unmapped_offset = append_records(bytes, unmapped);
  pad16(bytes);
  h.strings_offset = static_cast<std::uint32_t>(bytes.size());
  for (const char c : s.text) {
    bytes.push_back(static_cast<std::byte>(c));
  }
  pad16(bytes);
  write_at(bytes, 0, h);
  return bytes;
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

// (material_asset.hpp, decode_material_asset, MaterialAssetError order,
// MaterialAssetView): a valid payload reads back its template name, each
// find_* hit with its value and each miss, and string() over a record's
// slice; each row then breaks one rule, or two where the order decides, and
// names the first failure with its table and record: a header failure table
// 0, a section failure its section's table (5 the string table) and the
// template name table 5, each with record 0 (MaterialAssetFailure).
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
    const auto specular = view.find_scalar(m::kSpecularCoefficient);
    const auto fresnel = view.find_scalar(m::kFresnelR);
    require(specular.found && specular.value == 0.5F && fresnel.found && fresnel.value == 0.25F,
            "find_scalar hits return the values");
    const auto color = view.find_vector(m::kConstantColor);
    require(color.found && color.value == std::array{1.0F, 0.5F, 0.25F, 1.0F},
            "find_vector hit returns the value");
    const auto has_normal = view.find_switch(m::kHasNormalMap);
    const auto has_gloss = view.find_switch(m::kHasGloss);
    require(has_normal.found && has_normal.value && has_gloss.found && !has_gloss.value,
            "find_switch hits return 1 as true and 0 as false");
    require(!view.find_texture(m::kGlossTexture).found && !view.find_scalar(m::kFresnelA).found &&
                !view.find_vector(m::kReflectParameters).found &&
                !view.find_switch(m::kHasEnvironment).found,
            "each lookup misses a name its table lacks");
    require(!view.find_scalar(m::kConstantColor).found,
            "a lookup reads its own table only: a vector name is no scalar");
    const m::UnmappedFieldRecord first = view.unmapped_record(0);
    const m::UnmappedFieldRecord second = view.unmapped_record(1);
    require(view.string(first.name_offset, first.name_length) == kUnmappedName &&
                view.string(first.value_offset, first.value_length) == "0x3f800000" &&
                view.string(second.value_offset, second.value_length).empty(),
            "string() reads an unmapped field's name and value, an empty value as empty");
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
          E::TemplateNameInvalid, 5, 0},
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
      Row{"a texture id that does not validate (upper case)",
          [](Bytes& b) {
            const auto field = read_at<m::UnmappedFieldRecord>(b, header_of(b).unmapped_offset);
            patch_record<m::TextureRecord>(b, &H::textures_offset, 1, [&](m::TextureRecord& r) {
              r.id_offset = field.name_offset;
              r.id_length = field.name_length;
            });
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
