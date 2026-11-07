#ifndef BW_CONTENT_CONVERTER_MATERIAL_MAPPING_HPP
#define BW_CONTENT_CONVERTER_MATERIAL_MAPPING_HPP
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"
#include "bw/content_converter/mesh_reader.hpp"
#include "bw/content_converter/texture_reader.hpp"
#include "bw/rendering/materials/shading.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: the original material block to a material asset
// (docs/architecture/content.md, "Material mapping": the converter maps
// original material blocks onto templates, switches and parameters and
// records what it could not map by name, in a file of its own beside the
// asset) and the texture slot resolution behind it.
//
// Texture table: the sibling texture/default.cpr holds one CALL
// vfx.texture.create whose arguments are `table` then N × `dds`, each a
// qualified name; TR slot index k names the k-th `dds` argument, whose
// record is found by qualified name, ignoring case, in the model's own
// texture/texture.cpr or under the family's shared_texture directory. In
// all 841 tables the count equals the table._tm count and every argument
// resolves; the table._tm names are 31-character truncations of the same
// names and are not used.
//
// Mapping onto the lit template (bw/rendering/materials/lit_template.hpp),
// names from the table of material_asset.hpp. kMaterialMappingRevision
// numbers it: any change to a rule below changes the number, and the
// number is in every material's input hash (conversion.hpp), so a mapping
// change reconverts materials and nothing else.
//   Template: kLitTemplate for every material.
//   Slots, each a texture record and a switch record set when the slot is
//   present and clear otherwise: BTX0 -> base_color and base_color_map;
//   NMAP -> normal and normal_map; AMOC -> occlusion_reflectance and
//   occlusion_reflectance_map (the original's textures in that slot,
//   named gloss, hold ambient occlusion in the first channel and a
//   reflectance weight in the second, which is the template's packing, so
//   the texture is converted as it is). BTX1, BLDT, LENV and LUMX have no
//   lit parameter: a present BTX1 writes the switch kSecondBaseColorSwitch
//   = 1, BLDT kBlendMaskSwitch, LENV kEnvironmentMapSwitch and LUMX
//   kIlluminanceMapSwitch (no record when absent). No template declares
//   those four, so the material fails by that name when it is created
//   instead of drawing without the slot, and the slot's id is kept as the
//   unmapped field TR.<tag>.
//   CF1I:
//     CCOL      -> vector base_color_tint, as stored.
//     LUMI r    -> vector emissive = (r, r, r, 0); the template multiplies
//                  it by the base color.
//     SPCC n    -> scalar roughness = roughness_from_exponent(n).
//     SPCC, REFP x, FRSR -> the reflectance curve, written only when the
//                  block has all three records. With
//                  w = highlight_reflectance(SPCC n, SPCC k, REFP x):
//                  scalar reflectance = saturate(w * saturate(FRSR z)),
//                  scalar grazing_reflectance = saturate(w *
//                  saturate(FRSR y + FRSR z)),
//                  scalar fresnel_exponent = FRSR x.
//                  The default FRSR (1, 0, 1) gives both ends the value
//                  w: no change with the view angle.
//     REFP y    -> scalar specular_tint = saturate(REFP y).
//     REFP z    -> scalar reflectance_base_luminance = saturate(REFP z).
//     FRSA      -> scalars opacity_facing = saturate(FRSA z),
//                  opacity_grazing = saturate(FRSA y + FRSA z) and
//                  opacity_fresnel_exponent = FRSA x.
//     A block with no highlight (SPCC n <= 0 or k <= 0) reflects nothing
//     in the original. It gets roughness 1 and none of the reflectance
//     scalars, so the template's defaults apply (reflectance 0.04, the
//     model's own curve): every surface reflects a little.
//     REFP w and ENVP shape the environment reflection (how far the side
//     the key light misses still shows it; its tint). No lit parameter
//     reads them: the lit model has no environment term yet, and when it
//     has one a surface's reflection of it follows from its reflectance
//     and occlusion, not from a control of its own.
//     FRSR, FRSA, REFP, ENVP and SPCC are also each recorded whole as an
//     unmapped field by tag, the components mapped above included, so
//     every authored value is kept by name beside the cooked material.
//   Every material also carries scalar opacity_mask_threshold = 0.5.
//   SSSM's LENV middle word -> unmapped, as "SSSM.level", in decimal.
//   Blend (blend_mode_of): an SRS block tagged BTNO is Translucent and one
//   tagged BTAD is Additive; with no SRS block a base texture whose alpha
//   is Binary is Masked; everything else is Opaque. The scalar blend_mode
//   carries the BlendMode enumerator's value. A material with no SRS block
//   whose base alpha is Graded stays Opaque and gains the unmapped field
//   "base_alpha" with the value "graded", so the report counts them.
//   Unmapped, each recorded by name with its value as text, in this order:
//   the five CF1I tags above, in the order FRSR, FRSA, REFP, ENVP, SPCC,
//   and any CF1I tag the rules do not name (PARF,
//   always zero, and FRSB, always 1,0,1,0), each named by its tag with its
//   four floats as text separated by ','; "SSSM.level" when the block has
//   one; the knsS words as "knsS.feature" and "knsS.second", each "0x" and
//   eight lower-case hexadecimal digits; "base_alpha" as above; then TR
//   slots that have no lit parameter, the four above included, each named
//   "TR.<tag>" with the slot's id as text. A tag that appears twice
//   takes its first occurrence and the later one is unmapped; names are
//   unique within one file. Floats are written shortest
//   round-trip, locale-independent, so equal blocks give equal text on
//   every host.
//   Record order: switches base_color_map, normal_map,
//   occlusion_reflectance_map, then
//   base_color_1, blend_mask, environment_map and illuminance_map when
//   written; scalars blend_mode, roughness, then reflectance,
//   grazing_reflectance and fresnel_exponent when the block has a
//   highlight, specular_tint, reflectance_base_luminance, opacity_facing,
//   opacity_grazing, opacity_fresnel_exponent,
//   opacity_mask_threshold; vectors base_color_tint, emissive; textures in
//   the slot order above.
//
// Contract.
//   texture_table_from_default: the N qualified names in argument order,
//   or a named failure when the CALL is absent or an argument has another
//   slot name.
//   resolve_slot(table, k, resolver): the id of the k-th name folded to
//   lower case (asset_id_from_qualified_name); IndexOutOfRange when k is
//   at or past the table; the resolver says whether a record of that
//   qualified name exists (the conversion knows every container), and a
//   name that resolves nowhere is TextureMissing. Zero of 10,636 slots
//   miss in the shipped install.
//   classify_base_alpha(image): Opaque when every texel's alpha is 255;
//   Binary when every texel's alpha is 0 or 255 and at least one is 0;
//   Graded otherwise. NoTexture is the class of a material with no BTX0
//   slot and is never returned here.
//   roughness_from_exponent(n) = (2 / (n + 2)) ^ 0.25, and 1 when n <= 0.
//   highlight_reflectance(n, k, reflectivity) = kLightSpecularRatio * k *
//   reflectivity * 8 * (2^(-n / 2) + n) / ((n + 2) * (n + 4)), and 0 when
//   n <= 0 or k <= 0: the reflectance at normal incidence of a normalised
//   lobe as bright as the original's highlight under its key light.
//   map_material: the material asset and the unmapped fields of a block,
//   its resolved slot ids and its base alpha class, by the rules above; or
//     SwitchesNotAdmitted  the lit template's predicate rejects the
//                          switch values the slots and the blend mode
//                          give (name: the first switch whose requirement
//                          is not met, or "masked" when it is the masked
//                          requirement);
//     BlendMarkerUnknown   the block has an SRS block whose tag is neither
//                          BTNO nor BTAD (name: the tag).
//   On a failure `asset` and `unmapped` are empty.
//   Unmapped fields file: the converter's own record, text, one line per
//   field in the order above: the name, a tab, the value, '\n'. No name or
//   value the rules above produce holds a tab or a line end.
//   render_unmapped_fields writes it; parse_unmapped_fields reads it back
//   and returns false, leaving `out` empty, for text that does not end
//   with '\n' when it is not empty, a line with no tab, or an empty name.
//   The cooked material does not carry these fields and the game never
//   opens the file (conversion.hpp, "Write").
//
// A Phong exponent n and a Beckmann width a_b are very similar when
// n = 2 * a_b^-2 - 2, so a_b = sqrt(2 / (n + 2)); a = roughness^2, so
// roughness = sqrt(a), and 0.5 specular is 4 percent. The roughness rule
// takes the Beckmann width as the GGX width: equal lobe widths.
// A Blinn-Phong lobe that reflects all incident light at normal incidence
// is (n + 2)(n + 4) / (8 pi (2^(-n / 2) + n)) * (N.H)^n; the highlight
// conversion is built on it. The original's highlight
// is k * reflectivity * (N.H)^n times the key light's specular colour,
// which the content authors at kLightSpecularRatio times its diffuse
// colour (the plan records the 31 stage records: exactly 100 in 12, from
// 50 to 213 over the rest). A lobe of reflectance rho under a light whose
// diffuse response is albedo * colour * N.L returns rho * pi * that factor
// * (N.H)^n * colour at normal incidence; equating the two gives
// highlight_reflectance. A stage whose ratio is not 100 is a property of
// its light, for the lighting slice.
// The base color factor's runtime tint is not carried: the original
// multiplies base brightness by a per-object colour set at run time, and
// the weight reads base color alone. REFP z above 1 (2 materials) is
// clamped.
// Measured on PW0 at normal incidence, mask 1: body and belt 0.44, the
// second body material and the face 0.11, eyes and ornaments 0.018, the
// magazine 0.44, hair 0.19; the AMOC texture's mean second channel brings
// the body to 0.044 and hair to 0.028.
//
// Core guidelines read: I.1 and Con.5 (a pure mapping over the block and
// the table), E.27 (named failures), Enum.3. Performance guidelines read:
// none applies; recorded rather than cited.
namespace bw::content_converter {

inline constexpr std::string_view kTextureCreateCall = "vfx.texture.create";
inline constexpr std::string_view kTableSlot = "table";
inline constexpr std::string_view kDdsSlot = "dds";

enum class TextureTableError : std::uint8_t {
  None,
  CallMissing,
  ArgumentSlotUnexpected,
};
struct TextureTable {
  // Qualified names as the CALL stores them, in order.
  std::vector<std::string_view> names{};
};
[[nodiscard]] TextureTableError texture_table_from_default(const CprContainer& default_cpr,
                                                           TextureTable& out);

// Whether a record with this qualified name exists anywhere the
// conversion indexed; compared ignoring ASCII case.
struct RecordResolver {
  virtual ~RecordResolver() = default;
  [[nodiscard]] virtual bool exists(std::string_view qualified_name) const = 0;
};

enum class SlotError : std::uint8_t {
  None,
  IndexOutOfRange,
  TextureMissing,
  IdInvalid,
};
struct SlotResolution {
  content::AssetId id{};
  SlotError error{SlotError::None};
};
[[nodiscard]] SlotResolution resolve_slot(const TextureTable& table, std::uint32_t index,
                                          const RecordResolver& resolver);

inline constexpr std::uint32_t kMaterialMappingRevision = 3;

// Switch records the mapping writes for the four slots no template
// declares; the names are the converter's.
inline constexpr std::string_view kSecondBaseColorSwitch = "base_color_1";
inline constexpr std::string_view kBlendMaskSwitch = "blend_mask";
inline constexpr std::string_view kEnvironmentMapSwitch = "environment_map";
inline constexpr std::string_view kIlluminanceMapSwitch = "illuminance_map";

struct ResolvedSlot {
  std::array<char, 4> tag{};
  content::AssetId id{};
};

enum class BaseAlpha : std::uint8_t {
  NoTexture,
  Opaque,
  Binary,
  Graded,
};
[[nodiscard]] BaseAlpha classify_base_alpha(const Rgba8Image& image) noexcept;

[[nodiscard]] float roughness_from_exponent(float exponent) noexcept;

// The key light's specular colour over its diffuse colour, as the content
// authors it.
inline constexpr float kLightSpecularRatio = 100.0F;

[[nodiscard]] float highlight_reflectance(float exponent, float coefficient,
                                          float reflectivity) noexcept;

enum class MaterialMappingError : std::uint8_t {
  None,
  SwitchesNotAdmitted,
  BlendMarkerUnknown,
};

// The blend mode of a block and its base alpha class. BlendMarkerUnknown
// when the block's SRS tag is neither BTNO nor BTAD.
[[nodiscard]] MaterialMappingError blend_mode_of(const MaterialBlock& block, BaseAlpha base_alpha,
                                                 materials::BlendMode& out) noexcept;

struct UnmappedField {
  std::string name{};
  std::string value{};
};
// Appended to a material's cooked file path to name its unmapped fields
// file.
inline constexpr std::string_view kUnmappedFieldsSuffix = ".unmapped";
[[nodiscard]] std::string render_unmapped_fields(std::span<const UnmappedField> fields);
[[nodiscard]] bool parse_unmapped_fields(std::string_view text, std::vector<UnmappedField>& out);

struct MaterialMapping {
  MaterialAssetData asset{};
  std::vector<UnmappedField> unmapped{};
  MaterialMappingError error{MaterialMappingError::None};
  std::string name{};
  [[nodiscard]] bool ok() const noexcept { return error == MaterialMappingError::None; }
};

[[nodiscard]] MaterialMapping
map_material(const MaterialBlock& block, std::span<const ResolvedSlot> slots, BaseAlpha base_alpha);

} // namespace bw::content_converter

#endif
