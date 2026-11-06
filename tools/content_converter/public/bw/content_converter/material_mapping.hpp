#ifndef BW_CONTENT_CONVERTER_MATERIAL_MAPPING_HPP
#define BW_CONTENT_CONVERTER_MATERIAL_MAPPING_HPP
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"
#include "bw/content_converter/mesh_reader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: the original material block to a material asset
// (docs/architecture/materials.md, invariant 8 and "Content mapping": the
// converter maps original material blocks onto templates, switches and
// parameters and records what it could not map by name) and the texture
// slot resolution behind it.
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
// Mapping, one table in code (kMaterialMapping below states it as data):
//   TR BTX0 -> texture base_color, BTX1 -> base_color_1 and switch
//   has_base_color_1, BLDT -> blend_mask and has_blend_mask, NMAP -> normal
//   and has_normal_map, AMOC -> gloss and has_gloss (the slot name says
//   ambient occlusion and the textures are named gloss; recorded as gloss,
//   the conflict is the renderer's to resolve per family), LENV ->
//   environment and has_environment, LUMX -> illuminance_map and
//   has_illuminance_map.
//   CF1I CCOL -> vector constant_color; FRSR -> vector fresnel_r; FRSA ->
//   vector fresnel_a; LUMI -> scalar illuminance (the r component; the
//   content stores r,r,r,1); REFP -> vector reflect; SPCC -> vector
//   specular; ENVP -> vector environment_reflect_power.
//   SSSM's LENV middle word -> scalar subsurface_level.
//   Template: "lit" for every material this slice; the template set is
//   the renderer's and this slice records the mapping as data.
//   Unmapped, each recorded by name with its value as text, in this order:
//   CF1I records whose tag the table does not map (PARF, always zero, and
//   FRSB, always 1,0,1,0; no shader constant reads it), each named by its
//   tag with its four floats as text separated by ','; the knsS words as
//   "knsS.feature" and "knsS.second", each "0x" and eight lower-case
//   hexadecimal digits; "SRS" as its tag then ':' then its three words in
//   decimal separated by ',', when present; then TR slots whose tag the
//   table does not map, each named "TR.<tag>" with the slot's id as text. A
//   tag that appears twice takes its first occurrence and the later one is
//   unmapped; a name already used gains '.' and the field's 0-based
//   position in the unmapped list, so names stay unique. Floats are
//   written shortest round-trip, locale-independent, so equal blocks give
//   equal text on every host.
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
//   map_material: MaterialAssetData from a MaterialBlock and the resolved
//   slot ids, switches for every has_* name present or absent with the
//   value the slots give, parameters in kMaterialMapping order.
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

// The mapping as data, so a test and a reader can state it.
enum class MappedKind : std::uint8_t {
  Texture,
  Vector,
  Scalar,
};
struct MappingRow {
  std::array<char, 4> tag{};
  MappedKind kind{};
  std::string_view parameter{};
  // The switch set when the slot is present; empty for parameters.
  std::string_view switch_name{};
};
[[nodiscard]] std::span<const MappingRow> material_mapping() noexcept;

struct ResolvedSlot {
  std::array<char, 4> tag{};
  content::AssetId id{};
};

[[nodiscard]] MaterialAssetData map_material(const MaterialBlock& block,
                                             std::span<const ResolvedSlot> slots);

} // namespace bw::content_converter

#endif
