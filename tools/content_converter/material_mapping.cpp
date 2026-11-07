#include "bw/content_converter/material_mapping.hpp"

#include "bw/content/asset_id.hpp"
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"
#include "bw/content_converter/mesh_reader.hpp"
#include "bw/content_converter/texture_reader.hpp"
#include "bw/rendering/materials/lit_template.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/materials/shading.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
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
// sibling default.cpr, slot resolution, the base alpha class, the blend rule,
// the mapping of one material block onto the lit template (revision 3) and
// the unmapped fields file.
//
// Contract (the header's), with the readings it leaves open:
//   texture_table_from_default takes the first CALL whose short name is
//   vfx.texture.create (the CALL's short name is its function name); its
//   first argument must have the slot name `table` and every later one
//   `dds`, else ArgumentSlotUnexpected. The `dds` values are kept as the
//   CALL stores them, original case.
//   resolve_slot checks in SlotError order: the index, then the resolver,
//   then the folded id.
//   classify_base_alpha of an image with no texel is Opaque (every texel's
//   alpha is 255 vacuously).
//   roughness_from_exponent takes the fourth root as two square roots,
//   which are correctly rounded on every host, so equal blocks give equal
//   bytes everywhere.
//   highlight_reflectance is evaluated in double and rounded once to float:
//   2^(-n / 2) is the one transcendental step, and a double result rounds
//   to the same float on every host but in ties no exponent the content
//   stores reaches.
//   map_material: a CF1I tag the rules read is mapped from its first
//   record. A block with no SPCC record has no highlight. The three
//   reflectance-curve scalars need SPCC, REFP and FRSR together; when a
//   block with a highlight lacks REFP or FRSR, they are not written and the
//   template's defaults hold, as for a block with no highlight. When the
//   block has no CCOL, LUMI, REFP or FRSA record, the parameters that tag
//   gives are not written and the template's defaults hold. blend_mode,
//   roughness and opacity_mask_threshold are always written.
//   SwitchesNotAdmitted names occlusion_reflectance_map when that slot is
//   present without a base slot, and "masked" when the blend mode is Masked
//   without one (the masked dimension has no switch name of its own). Floats
//   are written by to_chars (shortest round-trip, locale-independent).
//   parse_unmapped_fields splits each line at its first tab; the value is
//   the rest of the line, possibly empty.
//
// A Phong exponent n and a Beckmann width a_b are very similar when
// a_b = sqrt(2 / (n + 2)); roughness = sqrt(a), and 0.5 specular is 4
// percent. The roughness rule takes the Beckmann width as the GGX width.
// The header states the lobe normalisation the highlight conversion is
// built on.
namespace bw::content_converter {

namespace {

namespace m = materials;
using Tag = std::array<char, 4>;

constexpr Tag kBtx0{'B', 'T', 'X', '0'};
constexpr Tag kNmap{'N', 'M', 'A', 'P'};
constexpr Tag kAmoc{'A', 'M', 'O', 'C'};
constexpr Tag kCcol{'C', 'C', 'O', 'L'};
constexpr Tag kLumi{'L', 'U', 'M', 'I'};
constexpr Tag kSpcc{'S', 'P', 'C', 'C'};
constexpr Tag kRefp{'R', 'E', 'F', 'P'};
constexpr Tag kFrsr{'F', 'R', 'S', 'R'};
constexpr Tag kFrsa{'F', 'R', 'S', 'A'};
constexpr Tag kEnvp{'E', 'N', 'V', 'P'};
// The tags recorded whole as unmapped fields, in the header's order, ahead
// of every tag the rules do not name.
constexpr std::array<Tag, 5> kKeptWhole{kFrsr, kFrsa, kRefp, kEnvp, kSpcc};
constexpr Tag kBtno{'B', 'T', 'N', 'O'};
constexpr Tag kBtad{'B', 'T', 'A', 'D'};

// The slots with a lit texture parameter, in record order, and the switch
// each sets.
struct LitSlot {
  Tag tag{};
  std::string_view texture{};
  std::string_view switch_name{};
};
constexpr std::array<LitSlot, 3> kLitSlots{{
    {kBtx0, m::kBaseColorTexture, m::kBaseColorMapSwitch},
    {kNmap, m::kNormalTexture, m::kNormalMapSwitch},
    {kAmoc, m::kOcclusionReflectanceTexture, m::kOcclusionReflectanceMapSwitch},
}};
// The slots with no lit parameter: a present one writes its switch = 1, in
// this order, so the material fails by that name when it is created.
struct UnreadSlot {
  Tag tag{};
  std::string_view switch_name{};
};
constexpr std::array<UnreadSlot, 4> kUnreadSlots{{
    {{'B', 'T', 'X', '1'}, kSecondBaseColorSwitch},
    {{'B', 'L', 'D', 'T'}, kBlendMaskSwitch},
    {{'L', 'E', 'N', 'V'}, kEnvironmentMapSwitch},
    {{'L', 'U', 'M', 'X'}, kIlluminanceMapSwitch},
}};

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

BaseAlpha classify_base_alpha(const Rgba8Image& image) noexcept {
  bool any_transparent = false;
  bool any_graded = false;
  for (std::size_t at = 3; at < image.pixels.size(); at += 4) {
    const std::uint8_t alpha = image.pixels[at];
    any_transparent = any_transparent || alpha == 0U;
    any_graded = any_graded || (alpha != 0U && alpha != 255U);
  }
  if (any_graded) {
    return BaseAlpha::Graded;
  }
  return any_transparent ? BaseAlpha::Binary : BaseAlpha::Opaque;
}

float roughness_from_exponent(float exponent) noexcept {
  if (!(exponent > 0.0F)) {
    return 1.0F;
  }
  return std::sqrt(std::sqrt(2.0F / (exponent + 2.0F)));
}

// The exponent then the coefficient, SPCC's own order.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
float highlight_reflectance(float exponent, float coefficient, float reflectivity) noexcept {
  if (!(exponent > 0.0F) || !(coefficient > 0.0F)) {
    return 0.0F;
  }
  const double n = exponent;
  const double lobe = 8.0 * (std::exp2(-n / 2.0) + n) / ((n + 2.0) * (n + 4.0));
  return static_cast<float>(static_cast<double>(kLightSpecularRatio) * coefficient * reflectivity *
                            lobe);
}

MaterialMappingError blend_mode_of(const MaterialBlock& block, BaseAlpha base_alpha,
                                   materials::BlendMode& out) noexcept {
  if (block.has_srs) {
    if (block.srs_tag == kBtno) {
      out = materials::BlendMode::Translucent;
    } else if (block.srs_tag == kBtad) {
      out = materials::BlendMode::Additive;
    } else {
      return MaterialMappingError::BlendMarkerUnknown;
    }
    return MaterialMappingError::None;
  }
  out =
      base_alpha == BaseAlpha::Binary ? materials::BlendMode::Masked : materials::BlendMode::Opaque;
  return MaterialMappingError::None;
}

MaterialMapping map_material(const MaterialBlock& block, std::span<const ResolvedSlot> slots,
                             BaseAlpha base_alpha) {
  MaterialMapping result;
  materials::BlendMode blend = materials::BlendMode::Opaque;
  if (blend_mode_of(block, base_alpha, blend) != MaterialMappingError::None) {
    result.error = MaterialMappingError::BlendMarkerUnknown;
    result.name = tag_text(block.srs_tag);
    return result;
  }
  const auto first_slot = [&](const Tag& tag) -> const ResolvedSlot* {
    for (const ResolvedSlot& slot : slots) {
      if (slot.tag == tag) {
        return &slot;
      }
    }
    return nullptr;
  };
  const ResolvedSlot* base = first_slot(kBtx0);
  const ResolvedSlot* packed = first_slot(kAmoc);
  const materials::LitSwitches switches{.base_color_map = base != nullptr,
                                        .normal_map = first_slot(kNmap) != nullptr,
                                        .occlusion_reflectance_map = packed != nullptr,
                                        .masked = blend == materials::BlendMode::Masked};
  if (!materials::lit_switches_admitted(switches)) {
    result.error = MaterialMappingError::SwitchesNotAdmitted;
    result.name = switches.occlusion_reflectance_map && !switches.base_color_map
                      ? std::string(m::kOcclusionReflectanceMapSwitch)
                      : std::string("masked");
    return result;
  }

  MaterialAssetData& out = result.asset;
  out.template_name = std::string(m::kLitTemplate);
  // Switches and textures.
  std::vector<bool> slot_used(slots.size(), false);
  for (const LitSlot& lit : kLitSlots) {
    const ResolvedSlot* slot = first_slot(lit.tag);
    out.switches.push_back(NamedSwitch{std::string(lit.switch_name), slot != nullptr});
    if (slot != nullptr) {
      slot_used[static_cast<std::size_t>(slot - slots.data())] = true;
      out.textures.push_back(NamedTexture{std::string(lit.texture), std::string(slot->id.view())});
    }
  }
  for (const UnreadSlot& unread : kUnreadSlots) {
    if (first_slot(unread.tag) != nullptr) {
      out.switches.push_back(NamedSwitch{std::string(unread.switch_name), true});
    }
  }

  // CF1I records, each mapped from its tag's first record.
  std::array<bool, 9> record_used{};
  const auto take_record = [&](const Tag& tag) -> const Cf1iRecord* {
    for (std::size_t k = 0; k < block.cf1i.size(); ++k) {
      if (block.cf1i[k].tag == tag) {
        record_used[k] = true;
        return &block.cf1i[k];
      }
    }
    return nullptr;
  };
  const Cf1iRecord* ccol = take_record(kCcol);
  const Cf1iRecord* lumi = take_record(kLumi);
  const Cf1iRecord* spcc = take_record(kSpcc);
  const Cf1iRecord* refp = take_record(kRefp);
  const Cf1iRecord* frsr = take_record(kFrsr);
  const Cf1iRecord* frsa = take_record(kFrsa);
  const auto saturate = [](float v) { return std::clamp(v, 0.0F, 1.0F); };
  const bool highlight = spcc != nullptr && spcc->value[0] > 0.0F && spcc->value[1] > 0.0F;
  const auto scalar = [&out](std::string_view name, float value) {
    out.scalars.push_back(NamedScalar{std::string(name), value});
  };
  scalar(m::kBlendModeParameter, static_cast<float>(static_cast<std::uint32_t>(blend)));
  scalar(m::kRoughness, highlight ? roughness_from_exponent(spcc->value[0]) : 1.0F);
  if (highlight && refp != nullptr && frsr != nullptr) {
    const float w = highlight_reflectance(spcc->value[0], spcc->value[1], refp->value[0]);
    scalar(m::kReflectance, saturate(w * saturate(frsr->value[2])));
    scalar(m::kGrazingReflectance, saturate(w * saturate(frsr->value[1] + frsr->value[2])));
    scalar(m::kFresnelExponent, frsr->value[0]);
  }
  if (refp != nullptr) {
    scalar(m::kSpecularTint, saturate(refp->value[1]));
    scalar(m::kReflectanceBaseLuminance, saturate(refp->value[2]));
  }
  if (frsa != nullptr) {
    scalar(m::kOpacityFacing, saturate(frsa->value[2]));
    scalar(m::kOpacityGrazing, saturate(frsa->value[1] + frsa->value[2]));
    scalar(m::kOpacityFresnelExponent, frsa->value[0]);
  }
  scalar(m::kOpacityMaskThreshold, 0.5F);
  if (ccol != nullptr) {
    out.vectors.push_back(NamedVector{std::string(m::kBaseColorTint), ccol->value});
  }
  if (lumi != nullptr) {
    const float r = lumi->value[0];
    out.vectors.push_back(NamedVector{std::string(m::kEmissive), {r, r, r, 0.0F}});
  }

  // Unmapped fields, in the header's order: the tags kept whole, then every
  // record no rule took, in block order.
  std::vector<UnmappedField>& unmapped = result.unmapped;
  for (const Tag& tag : kKeptWhole) {
    for (std::size_t k = 0; k < block.cf1i.size(); ++k) {
      if (block.cf1i[k].tag == tag) {
        record_used[k] = true;
        add_unmapped(unmapped, tag_text(tag), floats_text(block.cf1i[k].value));
        break;
      }
    }
  }
  for (std::size_t k = 0; k < block.cf1i.size(); ++k) {
    if (!record_used[k]) {
      add_unmapped(unmapped, tag_text(block.cf1i[k].tag), floats_text(block.cf1i[k].value));
    }
  }
  if (block.has_sssm) {
    std::string level;
    append_number(level, block.sssm_lenv[1]);
    add_unmapped(unmapped, "SSSM.level", std::move(level));
  }
  add_unmapped(unmapped, "knsS.feature", hex_text(block.knss_feature));
  add_unmapped(unmapped, "knsS.second", hex_text(block.knss_second));
  if (!block.has_srs && base_alpha == BaseAlpha::Graded) {
    add_unmapped(unmapped, "base_alpha", "graded");
  }
  for (std::size_t i = 0; i < slots.size(); ++i) {
    if (!slot_used[i]) {
      add_unmapped(unmapped, "TR." + tag_text(slots[i].tag), std::string(slots[i].id.view()));
    }
  }
  return result;
}

std::string render_unmapped_fields(std::span<const UnmappedField> fields) {
  std::string text;
  for (const UnmappedField& field : fields) {
    text += field.name;
    text += '\t';
    text += field.value;
    text += '\n';
  }
  return text;
}

bool parse_unmapped_fields(std::string_view text, std::vector<UnmappedField>& out) {
  out.clear();
  if (!text.empty() && !text.ends_with('\n')) {
    return false;
  }
  std::vector<UnmappedField> fields;
  while (!text.empty()) {
    const std::size_t end = text.find('\n');
    const std::string_view line = text.substr(0, end);
    text.remove_prefix(end + 1U);
    const std::size_t tab = line.find('\t');
    if (tab == std::string_view::npos || tab == 0U) {
      return false;
    }
    fields.push_back(
        UnmappedField{std::string(line.substr(0, tab)), std::string(line.substr(tab + 1U))});
  }
  out = std::move(fields);
  return true;
}

} // namespace bw::content_converter
