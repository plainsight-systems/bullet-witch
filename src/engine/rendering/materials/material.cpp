#include "bw/rendering/materials/material.hpp"

#include "bw/rendering/materials/lit_template.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/materials/shading.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

// Responsibility: the out-of-line part of bw/rendering/materials/material.hpp:
// the template registry, material creation from a cooked asset, and
// instance overrides.
//
// Contract, with the readings the header leaves to the implementation:
//   - A member is a parameter when it is not an array, its name does not
//     start with "reserved" and it is not the template's sampler member
//     (rule 2). A Float3 member takes the first three components of a
//     vector and its fourth word stays as it was (zero from the defaults).
//   - create_material checks, in this order, each failure ending it: the
//     template, unknown switches, missing switches, the blend mode, the
//     predicate, every scalar, vector and texture record against rule 2,
//     missing textures, unused textures, unbound textures. The header does
//     not promise an order; this is the one the code runs.
//   - The blend_mode scalar names a blend mode when it is a whole number
//     below kBlendModeCount whose bit is in the template's blend_modes;
//     anything else is BlendModeUnsupported, named by the value as the
//     shortest text that reads back to it.
//   - An instance's switch values are not stored: set_texture finds them as
//     the one value tuple whose permutation, with the instance's blend
//     mode, is the instance's permutation (at most 2^kMaxMaterialSwitches
//     calls of the template's pure function).
//
// Numeric parameter values are written into the material's block. An
// instance reads its own value, else its parent's, with no compilation on
// that path. A material never changes after creation, so an instance copies
// its parent's block once, at creation, rather than looking the parent up
// at read time. No template the game needs has a subsurface, specular or
// toon profile, so no profile parameter is answered ahead of the
// instance's own values.
namespace bw::materials {
namespace {

using shaders::MemberLayout;
using shaders::ParameterKind;

[[nodiscard]] MaterialFailure failed(MaterialError error, std::string_view name) {
  return MaterialFailure{error, std::string(name)};
}

[[nodiscard]] bool is_parameter(const MaterialTemplate& material_template,
                                const MemberLayout& member) noexcept {
  return member.element_count == 0U && !member.name.starts_with("reserved") &&
         member.name != material_template.sampler;
}

// The parameter member of that name and one of the given kinds, or null.
[[nodiscard]] const MemberLayout* find_parameter(const MaterialTemplate& material_template,
                                                 std::string_view name, ParameterKind first,
                                                 ParameterKind second) noexcept {
  for (const MemberLayout& member : material_template.block->members) {
    if (member.name == name && (member.kind == first || member.kind == second) &&
        is_parameter(material_template, member)) {
      return &member;
    }
  }
  return nullptr;
}

[[nodiscard]] const MemberLayout* find_member(const MaterialTemplate& material_template,
                                              std::string_view name) noexcept {
  for (const MemberLayout& member : material_template.block->members) {
    if (member.name == name) {
      return &member;
    }
  }
  return nullptr;
}

// The template's texture parameter of that name, or null.
[[nodiscard]] const MaterialTextureParameter*
find_texture_parameter(const MaterialTemplate& material_template, std::string_view name) noexcept {
  for (const MaterialTextureParameter& texture : material_template.textures) {
    if (texture.name == name) {
      return &texture;
    }
  }
  return nullptr;
}

// A texture member: a TextureIndex parameter the template's textures list.
[[nodiscard]] const MemberLayout* find_texture_member(const MaterialTemplate& material_template,
                                                      std::string_view name) noexcept {
  if (find_texture_parameter(material_template, name) == nullptr) {
    return nullptr;
  }
  return find_parameter(material_template, name, ParameterKind::TextureIndex,
                        ParameterKind::TextureIndex);
}

// Writes `count` 32-bit words of `words` at the member's offset.
void write_words(std::span<std::byte> block, const MemberLayout& member, const void* words,
                 std::size_t count) noexcept {
  std::memcpy(block.data() + member.offset, words, count * sizeof(std::uint32_t));
}

void write_vector(std::span<std::byte> block, const MemberLayout& member,
                  const std::array<float, 4>& value) noexcept {
  write_words(block, member, value.data(), member.kind == ParameterKind::Float3 ? 3U : 4U);
}

[[nodiscard]] std::string float_text(float value) {
  std::array<char, 32> buffer{};
  const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  return error == std::errc{} ? std::string(buffer.data(), end) : std::string{};
}

// The blend mode a blend_mode scalar names for this template, or no value.
[[nodiscard]] std::optional<BlendMode> blend_mode_of(const MaterialTemplate& material_template,
                                                     float value) noexcept {
  for (std::uint32_t mode = 0; mode < kBlendModeCount; ++mode) {
    if (value == static_cast<float>(mode)) {
      if ((material_template.blend_modes & (1U << mode)) == 0U) {
        return std::nullopt;
      }
      return static_cast<BlendMode>(mode);
    }
  }
  return std::nullopt;
}

} // namespace

std::span<const MaterialTemplate* const> material_templates() noexcept {
  static const std::array<const MaterialTemplate*, 1> kTemplates{&lit_material_template()};
  return kTemplates;
}

const MaterialTemplate* find_material_template(std::string_view name) noexcept {
  for (const MaterialTemplate* material_template : material_templates()) {
    if (material_template->name == name) {
      return material_template;
    }
  }
  return nullptr;
}

MaterialFailure create_material(const MaterialAssetView& asset,
                                std::span<const MaterialTextureBinding> textures,
                                shaders::SamplerIndex sampler, Material& out) {
  out = Material{};
  const MaterialTemplate* material_template = find_material_template(asset.template_name());
  if (material_template == nullptr) {
    return failed(MaterialError::TemplateUnknown, asset.template_name());
  }
  const MaterialTemplate& t = *material_template;
  const MaterialPayloadHeader& header = asset.header();

  // Switches: every record names a declared switch; every declared switch
  // has a record.
  for (std::uint32_t i = 0; i < header.switch_count; ++i) {
    const SwitchRecord record = asset.switch_record(i);
    const std::string_view name = asset.string(record.name_offset, record.name_length);
    if (std::find(t.switches.begin(), t.switches.end(), name) == t.switches.end()) {
      return failed(MaterialError::SwitchUnknown, name);
    }
  }
  std::array<bool, kMaxMaterialSwitches> values{};
  for (std::size_t s = 0; s < t.switches.size(); ++s) {
    const MaterialAssetView::SwitchLookup found = asset.find_switch(t.switches[s]);
    if (!found.found) {
      return failed(MaterialError::SwitchMissing, t.switches[s]);
    }
    values[s] = found.value;
  }
  const std::span<const bool> switch_values{values.data(), t.switches.size()};

  // Blend mode and permutation.
  BlendMode blend = BlendMode::Opaque;
  const MaterialAssetView::ScalarLookup blend_scalar = asset.find_scalar(kBlendModeParameter);
  if (blend_scalar.found) {
    const std::optional<BlendMode> mode = blend_mode_of(t, blend_scalar.value);
    if (!mode.has_value()) {
      return failed(MaterialError::BlendModeUnsupported, float_text(blend_scalar.value));
    }
    blend = *mode;
  }
  const std::optional<shaders::PermutationId> permutation = t.permutation(switch_values, blend);
  if (!permutation.has_value()) {
    return failed(MaterialError::PermutationNotAdmitted, {});
  }

  // Parameters, written over the defaults.
  Material built;
  built.template_ = &t;
  built.block_size_ = t.block->size;
  built.blend_mode_ = blend;
  built.permutation_ = *permutation;
  std::copy(t.defaults.begin(), t.defaults.end(), built.block_.begin());
  const std::span<std::byte> block{built.block_.data(), built.block_size_};
  for (std::uint32_t i = 0; i < header.scalar_count; ++i) {
    const ScalarRecord record = asset.scalar_record(i);
    const std::string_view name = asset.string(record.name_offset, record.name_length);
    if (name == kBlendModeParameter) {
      continue;
    }
    const MemberLayout* member =
        find_parameter(t, name, ParameterKind::Float, ParameterKind::Float);
    if (member == nullptr) {
      return failed(MaterialError::ParameterUnknown, name);
    }
    write_words(block, *member, &record.value, 1);
  }
  for (std::uint32_t i = 0; i < header.vector_count; ++i) {
    const VectorRecord record = asset.vector_record(i);
    const std::string_view name = asset.string(record.name_offset, record.name_length);
    const MemberLayout* member =
        find_parameter(t, name, ParameterKind::Float4, ParameterKind::Float3);
    if (member == nullptr) {
      return failed(MaterialError::ParameterUnknown, name);
    }
    write_vector(block, *member, record.value);
  }
  for (std::uint32_t i = 0; i < header.texture_count; ++i) {
    const TextureRecord record = asset.texture_record(i);
    const std::string_view name = asset.string(record.name_offset, record.name_length);
    if (find_texture_member(t, name) == nullptr) {
      return failed(MaterialError::ParameterUnknown, name);
    }
  }

  // Textures: present exactly when their switch is set, and each resolved
  // by the caller.
  for (const MaterialTextureParameter& texture : t.textures) {
    if (switch_values[texture.switch_index] && !asset.find_texture(texture.name).found) {
      return failed(MaterialError::TextureMissing, texture.name);
    }
  }
  for (std::uint32_t i = 0; i < header.texture_count; ++i) {
    const TextureRecord record = asset.texture_record(i);
    const std::string_view name = asset.string(record.name_offset, record.name_length);
    if (!switch_values[find_texture_parameter(t, name)->switch_index]) {
      return failed(MaterialError::TextureUnused, name);
    }
  }
  for (std::uint32_t i = 0; i < header.texture_count; ++i) {
    const TextureRecord record = asset.texture_record(i);
    const std::string_view name = asset.string(record.name_offset, record.name_length);
    const auto bound =
        std::find_if(textures.begin(), textures.end(),
                     [name](const MaterialTextureBinding& b) { return b.parameter == name; });
    if (bound == textures.end()) {
      return failed(MaterialError::TextureUnbound, name);
    }
    write_words(block, *find_texture_member(t, name), &bound->texture.value, 1);
  }
  const MemberLayout* sampler_member = find_member(t, t.sampler);
  if (sampler_member != nullptr) {
    write_words(block, *sampler_member, &sampler.value, 1);
  }
  out = built;
  return {};
}

MaterialError MaterialInstance::set_scalar(std::string_view name, float value) noexcept {
  const MaterialTemplate& t = *material_.template_;
  const MemberLayout* member = find_parameter(t, name, ParameterKind::Float, ParameterKind::Float);
  if (member == nullptr) {
    return MaterialError::ParameterUnknown;
  }
  write_words({material_.block_.data(), material_.block_size_}, *member, &value, 1);
  return MaterialError::None;
}

MaterialError MaterialInstance::set_vector(std::string_view name,
                                           const std::array<float, 4>& value) noexcept {
  const MaterialTemplate& t = *material_.template_;
  const MemberLayout* member =
      find_parameter(t, name, ParameterKind::Float4, ParameterKind::Float3);
  if (member == nullptr) {
    return MaterialError::ParameterUnknown;
  }
  write_vector({material_.block_.data(), material_.block_size_}, *member, value);
  return MaterialError::None;
}

MaterialError MaterialInstance::set_texture(std::string_view name,
                                            shaders::TextureIndex texture) noexcept {
  const MaterialTemplate& t = *material_.template_;
  const MemberLayout* member = find_texture_member(t, name);
  if (member == nullptr) {
    return MaterialError::ParameterUnknown;
  }
  // The instance's switch values: the tuple whose permutation is its own.
  const std::size_t switch_count = t.switches.size();
  std::array<bool, kMaxMaterialSwitches> values{};
  bool found = false;
  for (std::uint32_t bits = 0; bits < (1U << switch_count) && !found; ++bits) {
    for (std::size_t s = 0; s < switch_count; ++s) {
      values[s] = ((bits >> s) & 1U) != 0U;
    }
    const std::optional<shaders::PermutationId> id =
        t.permutation({values.data(), switch_count}, material_.blend_mode_);
    found = id.has_value() && *id == material_.permutation_;
  }
  if (!found || !values[find_texture_parameter(t, name)->switch_index]) {
    return MaterialError::TextureUnused;
  }
  write_words({material_.block_.data(), material_.block_size_}, *member, &texture.value, 1);
  return MaterialError::None;
}

} // namespace bw::materials
