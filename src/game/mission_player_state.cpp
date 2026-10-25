// Apply source Record/checkpoint inheritance once per mission, keeping masks
// separate from permanent ownership. Concept entry always discards a supplied
// campaign checkpoint. Development origin is explicit and never saved.
// Repair unavailable weapon selection in original cyclic order; publish later
// selections only when callers have completed their resource/action
// transaction. Fixed storage and at most four probes, no allocations on
// success. See header. Design C.9/I.30; implementation E.4
// (#re-design-invariants) checks before changing state. Both-phase MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) bounds steady-state
// work.
#include "game/mission_player_state.hpp"

#include <algorithm>
#include <stdexcept>

namespace bw::game {
namespace {
// Source command bits deliberately swap Thunder and Tornado relative to the
// persistent array order. This mapping is a compatibility boundary.
constexpr std::array<std::uint32_t, player_spell_count> kSpellBits{
    1, 2, 4, 8, 16, 32, 128, 64, 256};
constexpr std::uint32_t kAllSpellBits = 0x1ff;
constexpr std::size_t kStorySpellBegin = 6;
}  // namespace
Progression development_completed_progression() {
  Progression result;
  // Authored development profile: acquired at minimum positive levels. These
  // are neither universal completion values nor grants made by original code.
  result.levels.weapons.fill(1);
  result.levels.spells.fill(1);
  return result;
}
MissionPlayerState::MissionPlayerState(const MissionPlayerEntry& entry) {
  validate_progression(entry.progression);
  if (entry.concept_profile != ConceptProfile::saved &&
      entry.concept_profile != ConceptProfile::development_completed) {
    throw std::invalid_argument("Invalid concept profile policy");
  }
  levels_ = entry.progression.levels;
  if (entry.concept_mission &&
      entry.concept_profile == ConceptProfile::development_completed) {
    levels_ = development_completed_progression().levels;
    origin_ = ProgressionOrigin::development_completed;
  } else if (!entry.concept_mission && entry.checkpoint) {
    const auto& checkpoint = *entry.checkpoint;
    levels_ = merge_checkpoint_levels(levels_, checkpoint.levels);
    if (checkpoint.selected_weapon >= player_weapon_count) {
      throw std::invalid_argument("Invalid checkpoint weapon");
    }
    selected_weapon_ = checkpoint.selected_weapon;
    origin_ = ProgressionOrigin::checkpoint;
  }
  // Original runtime application treats the three story spells as booleans,
  // while preserving the original raw Record/checkpoint bytes outside runtime.
  for (std::size_t i = kStorySpellBegin; i < levels_.spells.size(); ++i) {
    levels_.spells.at(i) = std::min<std::uint8_t>(levels_.spells.at(i), 1);
  }
  if (std::ranges::none_of(levels_.weapons,
                           [](std::uint8_t level) { return level != 0; })) {
    levels_.weapons.front() = 1;
  }
  if (!weapon_available(selected_weapon_)) selected_weapon_ = next_weapon();
}
bool MissionPlayerState::weapon_available(unsigned weapon) const {
  return levels_.weapons.at(weapon) != 0;
}
unsigned MissionPlayerState::next_weapon() const {
  for (unsigned step = 1; step <= player_weapon_count; ++step) {
    const auto candidate =
        static_cast<unsigned>((selected_weapon_ + step) % player_weapon_count);
    if (weapon_available(candidate)) return candidate;
  }
  throw std::logic_error("Mission player has no available weapon");
}
void MissionPlayerState::select_weapon(unsigned weapon) {
  if (!weapon_available(weapon)) {
    throw std::invalid_argument("Weapon has not been acquired");
  }
  selected_weapon_ = weapon;
}
bool MissionPlayerState::spell_available(unsigned spell) const {
  return levels_.spells.at(spell) != 0 &&
         ((script_disabled_spells_ | context_disabled_spells_) &
          kSpellBits.at(spell)) == 0;
}
void MissionPlayerState::set_script_disabled_spells(std::uint32_t mask) {
  if ((mask & ~kAllSpellBits) != 0) {
    throw std::invalid_argument("Invalid mission spell mask");
  }
  script_disabled_spells_ = mask;
}
void MissionPlayerState::set_context_disabled_spells(std::uint32_t mask) {
  if ((mask & ~kAllSpellBits) != 0) {
    throw std::invalid_argument("Invalid contextual spell mask");
  }
  context_disabled_spells_ = mask;
}
}  // namespace bw::game
