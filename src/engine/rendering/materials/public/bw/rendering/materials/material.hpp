#ifndef BW_RENDERING_MATERIALS_MATERIAL_HPP
#define BW_RENDERING_MATERIALS_MATERIAL_HPP

#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/materials/shading.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Responsibility: a material template as the runtime describes it, a
// material built from a cooked material asset, an instance of a material,
// and the block a draw names an instance by (docs/architecture/
// materials.md, invariants 2, 3, 4 and 5 and "Parameters at runtime"). A
// material is data: one template, a value for every static switch, a blend
// mode and parameter values. Nothing here compiles, loads a texture or
// touches the GPU; material_buffer.hpp places blocks on the GPU.
//
// Contract.
//   Templates.
//   1. A MaterialTemplate states everything create_material checks an
//      asset against: its name, its shading model, its parameter block
//      layout, the block bytes of its declared defaults, the names of its
//      static switches in order, its texture parameters with the switch
//      that makes each one sampled, the blend modes it draws, and two
//      functions over its permutation domain. material_templates() is
//      every template, names unique; find_material_template finds one by
//      name or returns null.
//   2. A parameter is a member of the block: the asset names a parameter
//      by the member's name. A scalar record sets a Float member; a vector
//      record sets a Float4 member, or a Float3 member from its first
//      three components; a texture record sets a TextureIndex member that
//      the template's textures list. A member whose name begins
//      "reserved", an array member and the sampler member are not
//      parameters.
//   Materials.
//   3. create_material builds `out` from an asset or names one mismatch.
//      On success: material_template() is the template the asset names;
//      blend_mode() is the asset's blend_mode scalar (material_asset.hpp,
//      kBlendModeParameter: the BlendMode enumerator's value; it is not a
//      block member) or Opaque when the asset has none; permutation() is the template's permutation
//      for the asset's switch values and that blend mode; block() is the template's default block
//      with each parameter the asset names written at its member's offset, each texture member set
//      from `textures`, and the sampler member set to `sampler`. A parameter the asset omits keeps
//      the default. Bytes of the block outside every member are zero.
//   4. Each mismatch is one error, with the name concerned (with several
//      broken, which one is named is not promised):
//        TemplateUnknown        no template has the asset's template name;
//        SwitchUnknown          the asset carries a switch the template
//                               does not declare;
//        SwitchMissing          the template declares a switch the asset
//                               does not carry;
//        BlendModeUnsupported   the blend_mode scalar is not the value of
//                               a blend mode the template draws;
//        PermutationNotAdmitted the template's predicate rejects the
//                               switch values with that blend mode;
//        ParameterUnknown       a scalar, vector or texture record names
//                               no parameter of that kind (rule 2);
//        TextureMissing         a switch is set and the asset has no
//                               texture record for a texture that switch
//                               makes sampled;
//        TextureUnused          the asset has a texture record whose
//                               switch is clear;
//        TextureUnbound         `textures` has no entry for a texture
//                               record's parameter.
//      On a failure `out` is left empty.
//   5. A Material does not change after creation. It is a value: copies
//      are independent, and it borrows nothing from the asset.
//   Instances.
//   6. An instance made from a material has that material's template,
//      blend mode and permutation and a block equal to the material's.
//      set_scalar, set_vector and set_texture write one parameter by the
//      rules of 2 and change no other byte of the block; none of them
//      changes the permutation or the blend mode. ParameterUnknown when
//      the name is no parameter of that kind; TextureUnused when the
//      texture's switch is clear in the instance's permutation. A failed
//      set changes nothing. No call compiles or creates anything.
//   7. An override of a static switch is not an instance: there is no
//      call for it (materials.md, invariant 5).
//   Draws.
//   8. A MaterialReference is the block a per-draw block nests to name an
//      instance: the descriptor index of the buffer that holds material
//      blocks and the byte offset of this instance's block in it
//      (material_buffer.hpp fills it).
//
// An instance's value is its own, else the parent's, with no compilation on
// that path, and numeric parameters are written into the material's block.
// A material never changes, so an instance copies its parent's block once,
// at creation, rather than looking the parent up at read time. There is no
// material graph, so no parameter is computed from an expression. A switch
// or blend override is a different material, resolved by the converter.
// No material of the game reads a parameter shared across materials.
//
// Read guidance: I.1 and E.7 (every input of a material is an argument),
// E.27 (a named error and the name concerned), C.32 and I.11 (a template
// is observed through a pointer to static data; nothing is owned), Con.5;
// CACHE.2 (a block is at most kMaxMaterialBlockBytes, held inline).
namespace bw::materials {

inline constexpr std::uint32_t kMaxMaterialSwitches = 8;
inline constexpr std::uint32_t kMaxMaterialBlockBytes = 256;

// A texture parameter and the index, in MaterialTemplate::switches, of the
// switch that makes the template sample it.
struct MaterialTextureParameter {
  std::string_view name{};
  std::uint32_t switch_index{};
};

struct MaterialTemplate {
  std::string_view name{};
  ShadingModel shading_model{ShadingModel::Lit};
  // The parameter block; its member names are the parameter names.
  const shaders::BlockLayout* block{};
  // block->size bytes: the declared default of every member.
  std::span<const std::byte> defaults{};
  // The switch names an asset carries, in the order `permutation` takes
  // their values; at most kMaxMaterialSwitches.
  std::span<const std::string_view> switches{};
  std::span<const MaterialTextureParameter> textures{};
  // The SamplerIndex member every texture of the template is sampled with.
  std::string_view sampler{};
  // Bit (1 << BlendMode) for each blend mode the template draws.
  std::uint32_t blend_modes{};
  // The permutation for switch values in `switches` order and a blend
  // mode, or no value when the template's predicate rejects them.
  // Precondition: values.size() == switches.size().
  std::optional<shaders::PermutationId> (*permutation)(std::span<const bool> values,
                                                       BlendMode blend) noexcept {};
  // Bit (1 << geometry::VertexAttribute) for each vertex attribute the
  // permutation's shaders read. Precondition: the id came from
  // `permutation`.
  std::uint32_t (*vertex_attributes)(shaders::PermutationId permutation) noexcept {};
};

[[nodiscard]] std::span<const MaterialTemplate* const> material_templates() noexcept;
[[nodiscard]] const MaterialTemplate* find_material_template(std::string_view name) noexcept;

enum class MaterialError : std::uint8_t {
  None,
  TemplateUnknown,
  SwitchUnknown,
  SwitchMissing,
  BlendModeUnsupported,
  PermutationNotAdmitted,
  ParameterUnknown,
  TextureMissing,
  TextureUnused,
  TextureUnbound,
};

struct MaterialFailure {
  MaterialError error{MaterialError::None};
  // The template, switch, parameter or texture parameter concerned; the
  // blend_mode value as text for BlendModeUnsupported; empty for
  // PermutationNotAdmitted.
  std::string name{};
  [[nodiscard]] bool ok() const noexcept { return error == MaterialError::None; }
};

// One texture of an asset as the caller resolved it: the parameter name
// and the descriptor index of its view.
struct MaterialTextureBinding {
  std::string_view parameter{};
  shaders::TextureIndex texture{};
};

class MaterialInstance;

class Material {
public:
  Material() = default;
  // Null for an empty material.
  [[nodiscard]] const MaterialTemplate* material_template() const noexcept { return template_; }
  [[nodiscard]] BlendMode blend_mode() const noexcept { return blend_mode_; }
  [[nodiscard]] shaders::PermutationId permutation() const noexcept { return permutation_; }
  // The block's bytes: the template's block size, or empty.
  [[nodiscard]] std::span<const std::byte> block() const noexcept {
    return {block_.data(), block_size_};
  }

private:
  friend MaterialFailure create_material(const MaterialAssetView&,
                                         std::span<const MaterialTextureBinding>,
                                         shaders::SamplerIndex, Material&);
  friend class MaterialInstance;
  const MaterialTemplate* template_{};
  std::array<std::byte, kMaxMaterialBlockBytes> block_{};
  std::uint32_t block_size_{};
  shaders::PermutationId permutation_{};
  BlendMode blend_mode_{BlendMode::Opaque};
};

// Precondition: `asset` came from a decode_material_asset that returned
// None.
[[nodiscard]] MaterialFailure create_material(const MaterialAssetView& asset,
                                              std::span<const MaterialTextureBinding> textures,
                                              shaders::SamplerIndex sampler, Material& out);

class MaterialInstance {
public:
  MaterialInstance() = default;
  explicit MaterialInstance(const Material& parent) noexcept : material_(parent) {}

  [[nodiscard]] const MaterialTemplate* material_template() const noexcept {
    return material_.material_template();
  }
  [[nodiscard]] BlendMode blend_mode() const noexcept { return material_.blend_mode(); }
  [[nodiscard]] shaders::PermutationId permutation() const noexcept {
    return material_.permutation();
  }
  [[nodiscard]] std::span<const std::byte> block() const noexcept { return material_.block(); }

  // Each returns None, ParameterUnknown or, for set_texture, TextureUnused.
  // Precondition: the instance was made from a material.
  [[nodiscard]] MaterialError set_scalar(std::string_view name, float value) noexcept;
  [[nodiscard]] MaterialError set_vector(std::string_view name,
                                         const std::array<float, 4>& value) noexcept;
  [[nodiscard]] MaterialError set_texture(std::string_view name,
                                          shaders::TextureIndex texture) noexcept;

private:
  // The parent's values with this instance's overrides written over them.
  Material material_{};
};

// Offsets: buffer 0, offset 4, two reserved words; size 16.
struct alignas(shaders::kBlockAlignment) MaterialReference {
  shaders::BufferIndex buffer{};
  std::uint32_t offset{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
};
inline constexpr std::uint32_t kMaterialReferenceBytes = 16;
static_assert(sizeof(MaterialReference) == kMaterialReferenceBytes);

} // namespace bw::materials

using MaterialReference = bw::materials::MaterialReference;
BW_PARAMETER_BLOCK_LAYOUT(MaterialReference, BW_PARAMETER_MEMBER(MaterialReference, buffer),
                          BW_PARAMETER_MEMBER(MaterialReference, offset),
                          BW_PARAMETER_MEMBER(MaterialReference, reserved0),
                          BW_PARAMETER_MEMBER(MaterialReference, reserved1));
#endif
