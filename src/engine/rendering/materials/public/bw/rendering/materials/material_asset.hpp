#ifndef BW_RENDERING_MATERIALS_MATERIAL_ASSET_HPP
#define BW_RENDERING_MATERIALS_MATERIAL_ASSET_HPP
#include "bw/content/asset_id.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

// Responsibility: the material payload of a cooked asset and its reader
// (docs/architecture/content.md, "Asset kinds": template name, static
// switch values, parameter values, texture ids; docs/architecture/
// materials.md, invariants 2 and 8: a material is data, one template, a
// value for every static switch, parameter values; the converter maps
// original material blocks and records every field it leaves unmapped by
// name). The template set and the parameter blocks are the renderer's and
// do not exist yet; this payload records the mapping as named data, so a
// material slice reads it without a format change.
//
// Layout, little-endian, read by copy: MaterialPayloadHeader at 0; then,
// at 16-aligned offsets it names, switch records, scalar records, vector
// records, texture records, unmapped-field records, and the string table.
// Every name and id is a slice of the string table.
//
// Contract (decode refuses a payload that breaks a rule below and names a
// broken rule; with several broken, which one is named is not promised):
//   1. The header fits; sections are 16-aligned and within the payload.
//   2. template_name is a non-empty slice.
//   3. Every record's name is a non-empty slice; names are unique within
//      their table (NameDuplicate names the later record of the pair). An
//      unmapped record's value is a slice of the string table, possibly
//      empty (a value outside it is NameInvalid for that record). A
//      texture record's id is a slice of the string table that validates
//      as an asset id (TextureIdInvalid otherwise; content asset_id.hpp,
//      validate_asset_id, is the one definition of that rule).
//   4. A switch value is 0 or 1.
//   Record order is the converter's mapping order, so two conversions of
//   the same input give the same bytes; the reader looks up by name.
//   An unmapped field keeps its original value as text, so nothing the
//   content said is lost.
//
// Core guidelines read: SL.con.3, C.183, Con.5 (as mesh_asset.hpp),
// SL.str.2 (names are borrowed views over the payload). Performance
// guidelines read: none applies to a table of a dozen entries read once
// at load; recorded rather than cited.
namespace bw::materials {

static_assert(std::endian::native == std::endian::little);

inline constexpr std::uint32_t kMaterialSectionAlignment = 16;

struct MaterialPayloadHeader {
  std::uint32_t switch_count{};
  std::uint32_t scalar_count{};
  std::uint32_t vector_count{};
  std::uint32_t texture_count{};
  std::uint32_t unmapped_count{};
  std::uint32_t string_bytes{};
  std::uint32_t template_name_offset{};
  std::uint32_t template_name_length{};
  std::uint32_t switches_offset{};
  std::uint32_t scalars_offset{};
  std::uint32_t vectors_offset{};
  std::uint32_t textures_offset{};
  std::uint32_t unmapped_offset{};
  std::uint32_t strings_offset{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
};
static_assert(sizeof(MaterialPayloadHeader) == 64);
static_assert(std::is_trivially_copyable_v<MaterialPayloadHeader>);

struct SwitchRecord {
  std::uint32_t name_offset{};
  std::uint32_t name_length{};
  std::uint32_t value{};
  std::uint32_t reserved{};
};
struct ScalarRecord {
  std::uint32_t name_offset{};
  std::uint32_t name_length{};
  float value{};
  std::uint32_t reserved{};
};
struct VectorRecord {
  std::uint32_t name_offset{};
  std::uint32_t name_length{};
  std::array<float, 4> value{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
};
struct TextureRecord {
  std::uint32_t name_offset{};
  std::uint32_t name_length{};
  std::uint32_t id_offset{};
  std::uint32_t id_length{};
};
struct UnmappedFieldRecord {
  std::uint32_t name_offset{};
  std::uint32_t name_length{};
  std::uint32_t value_offset{};
  std::uint32_t value_length{};
};
static_assert(sizeof(SwitchRecord) == 16 && sizeof(ScalarRecord) == 16 &&
              sizeof(VectorRecord) == 32 && sizeof(TextureRecord) == 16 &&
              sizeof(UnmappedFieldRecord) == 16);

enum class MaterialAssetError : std::uint8_t {
  None,
  HeaderTruncated,
  SectionMisaligned,
  SectionOutOfBounds,
  TemplateNameInvalid,
  NameInvalid,
  NameDuplicate,
  SwitchValueInvalid,
  TextureIdInvalid,
};

struct MaterialAssetFailure {
  MaterialAssetError error{MaterialAssetError::None};
  // Which table (0 switches, 1 scalars, 2 vectors, 3 textures, 4 unmapped,
  // 5 the string table and the template name) and which record; 0 for a
  // header or section failure, which names the section's table.
  std::uint32_t table{};
  std::uint32_t index{};
};

class MaterialAssetView {
public:
  MaterialAssetView() = default;
  [[nodiscard]] const MaterialPayloadHeader& header() const noexcept { return header_; }
  [[nodiscard]] std::string_view template_name() const noexcept;
  // Preconditions: index below the matching count.
  [[nodiscard]] SwitchRecord switch_record(std::uint32_t index) const noexcept;
  [[nodiscard]] ScalarRecord scalar_record(std::uint32_t index) const noexcept;
  [[nodiscard]] VectorRecord vector_record(std::uint32_t index) const noexcept;
  [[nodiscard]] TextureRecord texture_record(std::uint32_t index) const noexcept;
  [[nodiscard]] UnmappedFieldRecord unmapped_record(std::uint32_t index) const noexcept;
  // A slice of the string table (offset, length), validated by decode.
  [[nodiscard]] std::string_view string(std::uint32_t offset, std::uint32_t length) const noexcept;
  // Lookup by name; nullopt-like: found is false when absent.
  struct TextureLookup {
    std::string_view id{};
    bool found{};
  };
  [[nodiscard]] TextureLookup find_texture(std::string_view name) const noexcept;
  struct ScalarLookup {
    float value{};
    bool found{};
  };
  [[nodiscard]] ScalarLookup find_scalar(std::string_view name) const noexcept;
  struct VectorLookup {
    std::array<float, 4> value{};
    bool found{};
  };
  [[nodiscard]] VectorLookup find_vector(std::string_view name) const noexcept;
  struct SwitchLookup {
    bool value{};
    bool found{};
  };
  [[nodiscard]] SwitchLookup find_switch(std::string_view name) const noexcept;

private:
  friend MaterialAssetFailure decode_material_asset(std::span<const std::byte>,
                                                    MaterialAssetView&) noexcept;
  std::span<const std::byte> payload_{};
  MaterialPayloadHeader header_{};
};

[[nodiscard]] MaterialAssetFailure decode_material_asset(std::span<const std::byte> payload,
                                                         MaterialAssetView& view) noexcept;

// The parameter and texture names the converter writes for the original
// material model, so a reader and the converter agree by one table
// (docs/architecture/materials.md, "Content mapping"). Base color is the
// slot the host samples this slice.
inline constexpr std::string_view kLitTemplate = "lit";
inline constexpr std::string_view kBaseColorTexture = "base_color";
inline constexpr std::string_view kSecondBaseColorTexture = "base_color_1";
inline constexpr std::string_view kBlendMaskTexture = "blend_mask";
inline constexpr std::string_view kNormalTexture = "normal";
inline constexpr std::string_view kGlossTexture = "gloss";
inline constexpr std::string_view kEnvironmentTexture = "environment";
inline constexpr std::string_view kIlluminanceTexture = "illuminance_map";
inline constexpr std::string_view kConstantColor = "constant_color";
inline constexpr std::string_view kFresnelR = "fresnel_r";
inline constexpr std::string_view kFresnelA = "fresnel_a";
inline constexpr std::string_view kIlluminance = "illuminance";
inline constexpr std::string_view kReflectParameters = "reflect";
inline constexpr std::string_view kSpecularCoefficient = "specular";
inline constexpr std::string_view kEnvironmentReflectPower = "environment_reflect_power";
inline constexpr std::string_view kSubsurfaceLevel = "subsurface_level";
inline constexpr std::string_view kHasNormalMap = "has_normal_map";
inline constexpr std::string_view kHasSecondBaseColor = "has_base_color_1";
inline constexpr std::string_view kHasBlendMask = "has_blend_mask";
inline constexpr std::string_view kHasGloss = "has_gloss";
inline constexpr std::string_view kHasEnvironment = "has_environment";
inline constexpr std::string_view kHasIlluminanceMap = "has_illuminance_map";

} // namespace bw::materials

#endif
