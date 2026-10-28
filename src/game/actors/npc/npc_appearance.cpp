// Translate source-owned appearance into private instance presentation data.
// See npc_appearance.hpp for borrowing, setup costs and both-phase I.1/MEM.9.
// Prefix matching and handle-alias replacement are the original texture-table
// algorithm, not material-name guesses. No shared asset or device mutation.
#include "npc_appearance.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <stdexcept>
namespace bw::game {
namespace {
constexpr std::size_t kWordBits = 32, kModelGroups = 256, kPaletteBits = 64;
constexpr std::uint32_t kDefaultCharacterGroups = 0x05000083;
// Source strtok skips empty fields. Valid authored decimal indexes reproduce
// its clamped final word and masked bit; reject malformed numeric input rather
// than inherit atoi's undefined overflow behavior.
void add_tokens(std::string_view text, std::span<std::uint32_t> words) {
  while (!text.empty()) {
    const auto split = text.find('-');
    const auto token = text.substr(0, split);
    if (!token.empty()) {
      std::uint32_t index{};
      const auto parsed = std::from_chars(std::to_address(token.begin()),
                                          std::to_address(token.end()), index);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != std::to_address(token.end())) {
        throw std::invalid_argument("Invalid NPC appearance index");
      }
      words[std::min<std::size_t>(index / kWordBits, words.size() - 1)] |=
          std::uint32_t{1} << (index % kWordBits);
    }
    if (split == std::string_view::npos) break;
    text.remove_prefix(split + 1);
  }
}
bool class_prefix(std::string_view family, std::string_view prefix) {
  return family.size() >= prefix.size() &&
         std::ranges::equal(family.substr(0, prefix.size()), prefix,
                            [](unsigned char left, unsigned char right) {
                              return std::tolower(left) == right;
                            });
}
bool customized_family(std::string_view family) {
  constexpr std::array excluded{"vh0", "vt0", "ef0", "ec0", "ed0", "eg0"};
  return !class_prefix(family, "pw") &&
         std::ranges::find(excluded, family) == excluded.end();
}
bool human_family(std::string_view family) {
  return class_prefix(family, "hm") || class_prefix(family, "hw") ||
         class_prefix(family, "hr");
}
}  // namespace
PreparedNpcAppearance PreparedNpcAppearance::prepare(
    const content::PlacementArchetype& archetype,
    const ResidentModelView& model, std::span<const NpcPaletteBinding> palettes,
    std::span<const ResidentTextureSlot> slots,
    const std::shared_ptr<memory::Domain>& domain) {
  PreparedNpcAppearance result(domain);
  // Body construction installs four words; the model's upper four words keep
  // their all-enabled initializer. Null and present-empty strings differ.
  std::array<std::uint32_t, kModelGroups / kWordBits> groups{};
  groups.fill(~std::uint32_t{});
  std::ranges::fill(std::span(groups).first(4), 0);
  groups[0] = kDefaultCharacterGroups;
  if (customized_family(archetype.model) && archetype.customization_present) {
    groups[0] = 1;
    add_tokens(archetype.customization, std::span(groups).first(4));
  }
  result.groups_ = groups;
  result.draws_.reserve(model.definition.get().draws().size());
  for (const auto& draw : model.definition.get().draws()) {
    if (draw.group >= kModelGroups) {
      throw std::invalid_argument("NPC draw group exceeds native mask");
    }
    result.draws_.push_back(
        {(groups.at(draw.group / kWordBits) &
          (std::uint32_t{1} << (draw.group % kWordBits))) != 0});
  }
  if (!human_family(archetype.model)) return result;
  std::array<std::uint32_t, 2> selected{};
  add_tokens(archetype.secondary_customization, selected);
  memory::Vector<render::WorldTextureHandle> resolved{
      memory::Allocator<render::WorldTextureHandle>(domain)};
  resolved.reserve(slots.size());
  for (const auto& slot : slots) resolved.push_back(slot.texture);
  constexpr std::size_t kQueueCapacity = 16;
  std::size_t queued = 0;
  for (std::size_t variation = 0;
       variation < kPaletteBits && queued < kQueueCapacity; ++variation) {
    if ((selected.at(variation / kWordBits) &
         (std::uint32_t{1} << (variation % kWordBits))) == 0) {
      continue;
    }
    const auto binding =
        std::ranges::find_if(palettes, [&](const auto& candidate) {
          return candidate.family == archetype.model &&
                 candidate.variation == variation;
        });
    if (binding == palettes.end() || binding->baseline) continue;
    ++queued;
    if (binding->material.empty() || binding->texture.value == 0) {
      throw std::invalid_argument("Invalid NPC palette binding");
    }
    // Compare the supplied prefix case-sensitively, then replace every alias of
    // that slot's current handle. Subsequent entries see prior replacements.
    for (std::size_t index = 0; index < slots.size(); ++index) {
      if (!slots[index].name.starts_with(binding->material)) continue;
      const auto previous = resolved.at(index);
      std::ranges::replace(resolved, previous, binding->texture);
    }
  }
  result.texture_overrides_.reserve(resolved.size());
  for (std::size_t index = 0; index < resolved.size(); ++index) {
    if (resolved.at(index) != slots[index].texture) {
      result.texture_overrides_.push_back({index, resolved.at(index)});
    }
  }
  return result;
}
PreparedNpcAppearance PreparedNpcAppearance::default_weapon(
    const ResidentModelView& model,
    const std::shared_ptr<memory::Domain>& domain) {
  PreparedNpcAppearance result(domain);
  result.groups_.fill(~std::uint32_t{});
  result.draws_.reserve(model.definition.get().draws().size());
  for (const auto& draw : model.definition.get().draws()) {
    if (draw.group >= kModelGroups) {
      throw std::invalid_argument("Weapon draw group exceeds native mask");
    }
    result.draws_.push_back({true});
  }
  return result;
}
}  // namespace bw::game
