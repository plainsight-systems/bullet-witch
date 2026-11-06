#include "bw/content_converter/material_mapping.hpp"

#include "bw/content/asset_id.hpp"
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"
#include "bw/content_converter/mesh_reader.hpp"
#include "bw/rendering/materials/material_asset.hpp"

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

// Responsibility: the out-of-line part of
// bw/content_converter/material_mapping.hpp: the texture table read from the
// sibling default.cpr, slot resolution, the mapping table and the mapping
// of one material block.
//
// Contract (the header's), with the readings it leaves open:
//   texture_table_from_default takes the first CALL whose short name is
//   vfx.texture.create (the CALL's short name is its function name); its
//   first argument must have the slot name `table` and every later one
//   `dds`, else ArgumentSlotUnexpected. The `dds` values are kept as the
//   CALL stores them, original case.
//   resolve_slot checks in SlotError order: the index, then the resolver,
//   then the folded id.
//   map_material: switches, textures and parameters follow kMaterialMapping
//   order. A tag that appears twice takes its first occurrence; the later
//   one is unmapped. Unmapped fields come in this order: CF1I records whose
//   tag the table does not map (PARF and FRSB in the shipped content), each
//   named by its tag with its four floats as text separated by ','; the knsS
//   words as "knsS.feature" and "knsS.second" in hexadecimal, written "0x"
//   and eight lower-case digits; "SRS" with its tag then ':' then its three
//   words when present, the words in decimal separated by ',' (the header
//   leaves that spelling open); then TR slots whose tag the table does not
//   map, each named "TR.<tag>" with the slot's id as text. A name already
//   used gains '.' and the field's position (from 0) in the unmapped list,
//   so names stay unique as the payload requires. Floats are written by to_chars
//   (shortest round-trip, locale-independent), so equal blocks give equal
//   text on every host.
namespace bw::content_converter {

namespace {

namespace m = materials;
using Tag = std::array<char, 4>;

constexpr std::array<MappingRow, 15> kMaterialMapping{{
    {{'B', 'T', 'X', '0'}, MappedKind::Texture, m::kBaseColorTexture, {}},
    {{'B', 'T', 'X', '1'}, MappedKind::Texture, m::kSecondBaseColorTexture, m::kHasSecondBaseColor},
    {{'B', 'L', 'D', 'T'}, MappedKind::Texture, m::kBlendMaskTexture, m::kHasBlendMask},
    {{'N', 'M', 'A', 'P'}, MappedKind::Texture, m::kNormalTexture, m::kHasNormalMap},
    {{'A', 'M', 'O', 'C'}, MappedKind::Texture, m::kGlossTexture, m::kHasGloss},
    {{'L', 'E', 'N', 'V'}, MappedKind::Texture, m::kEnvironmentTexture, m::kHasEnvironment},
    {{'L', 'U', 'M', 'X'}, MappedKind::Texture, m::kIlluminanceTexture, m::kHasIlluminanceMap},
    {{'C', 'C', 'O', 'L'}, MappedKind::Vector, m::kConstantColor, {}},
    {{'F', 'R', 'S', 'R'}, MappedKind::Vector, m::kFresnelR, {}},
    {{'F', 'R', 'S', 'A'}, MappedKind::Vector, m::kFresnelA, {}},
    {{'L', 'U', 'M', 'I'}, MappedKind::Scalar, m::kIlluminance, {}},
    {{'R', 'E', 'F', 'P'}, MappedKind::Vector, m::kReflectParameters, {}},
    {{'S', 'P', 'C', 'C'}, MappedKind::Vector, m::kSpecularCoefficient, {}},
    {{'E', 'N', 'V', 'P'}, MappedKind::Vector, m::kEnvironmentReflectPower, {}},
    // SSSM's LENV entry: its middle word.
    {{'S', 'S', 'S', 'M'}, MappedKind::Scalar, m::kSubsurfaceLevel, {}},
}};
constexpr Tag kSssm{'S', 'S', 'S', 'M'};

std::string tag_text(const Tag& tag) { return {tag.data(), tag.size()}; }

void append_number(std::string& out, float value) {
  std::array<char, 32> buffer{};
  const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  // A float's shortest form always fits in 32 characters.
  if (error == std::errc{}) {
    out.append(buffer.data(), end);
  }
}

void append_number(std::string& out, std::uint32_t value) {
  std::array<char, 16> buffer{};
  const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  if (error == std::errc{}) {
    out.append(buffer.data(), end);
  }
}

std::string floats_text(const std::array<float, 4>& values) {
  std::string text;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0U) {
      text += ',';
    }
    append_number(text, values[i]);
  }
  return text;
}

std::string hex_text(std::uint32_t value) {
  std::array<char, 8> buffer{};
  const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, 16);
  std::string digits = error == std::errc{} ? std::string(buffer.data(), end) : std::string{};
  return "0x" + std::string(8U - digits.size(), '0') + digits;
}

void add_unmapped(std::vector<UnmappedField>& unmapped, std::string name, std::string value) {
  const bool used = std::any_of(unmapped.begin(), unmapped.end(),
                                [&](const UnmappedField& u) { return u.name == name; });
  if (used) {
    name += '.';
    append_number(name, static_cast<std::uint32_t>(unmapped.size()));
  }
  unmapped.push_back(UnmappedField{std::move(name), std::move(value)});
}

} // namespace

TextureTableError texture_table_from_default(const CprContainer& default_cpr, TextureTable& out) {
  out = TextureTable{};
  const CprRecord* call = nullptr;
  for (const CprRecord& record : default_cpr.records()) {
    if (record.tag == RecordTag::Call && record.short_name == kTextureCreateCall) {
      call = &record;
      break;
    }
  }
  if (call == nullptr) {
    return TextureTableError::CallMissing;
  }
  const std::span<const CprArgument> arguments = default_cpr.arguments_of(*call);
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    if (arguments[i].slot_name != (i == 0U ? kTableSlot : kDdsSlot)) {
      out = TextureTable{};
      return TextureTableError::ArgumentSlotUnexpected;
    }
    if (i != 0U) {
      out.names.push_back(arguments[i].value);
    }
  }
  return TextureTableError::None;
}

SlotResolution resolve_slot(const TextureTable& table, std::uint32_t index,
                            const RecordResolver& resolver) {
  if (index >= table.names.size()) {
    return {{}, SlotError::IndexOutOfRange};
  }
  const std::string_view name = table.names[index];
  if (!resolver.exists(name)) {
    return {{}, SlotError::TextureMissing};
  }
  content::AssetIdResult id = content::asset_id_from_qualified_name(name);
  if (!id.ok()) {
    return {{}, SlotError::IdInvalid};
  }
  return {std::move(id.id), SlotError::None};
}

std::span<const MappingRow> material_mapping() noexcept { return kMaterialMapping; }

MaterialAssetData map_material(const MaterialBlock& block, std::span<const ResolvedSlot> slots) {
  MaterialAssetData out;
  out.template_name = std::string(m::kLitTemplate);
  std::vector<bool> slot_used(slots.size(), false);
  std::array<bool, 9> record_used{};
  const auto first_slot = [&](const Tag& tag) -> const ResolvedSlot* {
    for (std::size_t i = 0; i < slots.size(); ++i) {
      if (slots[i].tag == tag) {
        return &slots[i];
      }
    }
    return nullptr;
  };
  const auto first_record = [&](const Tag& tag) -> std::size_t {
    for (std::size_t k = 0; k < block.cf1i.size(); ++k) {
      if (block.cf1i[k].tag == tag) {
        return k;
      }
    }
    return block.cf1i.size();
  };
  for (const MappingRow& row : kMaterialMapping) {
    if (row.kind == MappedKind::Texture) {
      const ResolvedSlot* slot = first_slot(row.tag);
      if (!row.switch_name.empty()) {
        out.switches.push_back(NamedSwitch{std::string(row.switch_name), slot != nullptr});
      }
      if (slot != nullptr) {
        slot_used[static_cast<std::size_t>(slot - slots.data())] = true;
        out.textures.push_back(
            NamedTexture{std::string(row.parameter), std::string(slot->id.view())});
      }
    } else if (row.tag == kSssm) {
      if (block.has_sssm) {
        out.scalars.push_back(
            NamedScalar{std::string(row.parameter), static_cast<float>(block.sssm_lenv[1])});
      }
    } else {
      const std::size_t k = first_record(row.tag);
      if (k == block.cf1i.size()) {
        continue;
      }
      record_used[k] = true;
      if (row.kind == MappedKind::Vector) {
        out.vectors.push_back(NamedVector{std::string(row.parameter), block.cf1i[k].value});
      } else {
        out.scalars.push_back(NamedScalar{std::string(row.parameter), block.cf1i[k].value[0]});
      }
    }
  }
  for (std::size_t k = 0; k < block.cf1i.size(); ++k) {
    if (!record_used[k]) {
      add_unmapped(out.unmapped, tag_text(block.cf1i[k].tag), floats_text(block.cf1i[k].value));
    }
  }
  add_unmapped(out.unmapped, "knsS.feature", hex_text(block.knss_feature));
  add_unmapped(out.unmapped, "knsS.second", hex_text(block.knss_second));
  if (block.has_srs) {
    std::string value = tag_text(block.srs_tag) + ':';
    for (std::size_t i = 0; i < block.srs_value.size(); ++i) {
      if (i != 0U) {
        value += ',';
      }
      append_number(value, block.srs_value[i]);
    }
    add_unmapped(out.unmapped, "SRS", std::move(value));
  }
  for (std::size_t i = 0; i < slots.size(); ++i) {
    if (!slot_used[i]) {
      add_unmapped(out.unmapped, "TR." + tag_text(slots[i].tag), std::string(slots[i].id.view()));
    }
  }
  return out;
}

} // namespace bw::content_converter
