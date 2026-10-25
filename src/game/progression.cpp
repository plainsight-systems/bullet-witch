// Original progression rules over owned value candidates. Validate before all
// mutation; fixed source cost/reward tables are indexed only after bounds
// checks. No allocations on successful paths, I/O or shared state. See
// progression.hpp. Design C.9/I.30; implementation E.4 (#re-design-invariants)
// preserves input on rejection. Both-phase performance MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) keeps tables/values
// inline and bounded.
#include "game/progression.hpp"

#include <algorithm>
#include <span>
#include <stdexcept>

namespace bw::game {
namespace {
constexpr std::uint8_t kMaximumLevel = 3;
constexpr std::size_t kPurchasableSpells = 6;
using Costs = std::array<std::uint32_t, kMaximumLevel>;
constexpr std::array<Costs, player_stat_count> kStatCosts{
    {{1, 50, 200}, {1, 50, 200}}};
constexpr std::array<Costs, player_weapon_count> kWeaponCosts{
    {{1, 80, 200}, {40, 150, 300}, {50, 200, 400}, {300, 600, 900}}};
constexpr std::array<Costs, kPurchasableSpells> kSpellCosts{{{40, 150, 300},
                                                             {1, 80, 200},
                                                             {25, 150, 380},
                                                             {20, 180, 400},
                                                             {1, 50, 150},
                                                             {30, 150, 380}}};
constexpr std::array<std::array<std::uint32_t, 5>, 7> kRewards{
    {{200, 300, 450, 650, 750},
     {180, 250, 350, 550, 600},
     {150, 200, 250, 450, 500},
     {100, 150, 200, 350, 400},
     {90, 120, 180, 300, 350},
     {80, 100, 150, 250, 300},
     {70, 90, 120, 200, 250}}};
std::uint8_t& selected_level(PlayerLevels& levels, UpgradeSelection selection) {
  switch (selection.category) {
    case UpgradeCategory::stat:
      return levels.stats.at(selection.index);
    case UpgradeCategory::weapon:
      return levels.weapons.at(selection.index);
    case UpgradeCategory::spell:
      return levels.spells.at(selection.index);
  }
  throw std::invalid_argument("Invalid upgrade category");
}
}  // namespace
void validate_levels(const PlayerLevels& levels) {
  const auto valid = [](std::uint8_t value) { return value <= kMaximumLevel; };
  if (!std::ranges::all_of(levels.stats, valid) ||
      !std::ranges::all_of(levels.weapons, valid) ||
      !std::ranges::all_of(levels.spells, valid)) {
    throw std::invalid_argument("Progression level outside original range");
  }
}
void validate_progression(const Progression& progression) {
  validate_levels(progression.levels);
  if (progression.points > maximum_upgrade_points) {
    throw std::invalid_argument("Upgrade wallet exceeds original cap");
  }
}
std::optional<std::uint32_t> upgrade_cost(const Progression& progression,
                                          UpgradeSelection selection) {
  validate_progression(progression);
  // This small value copy permits one checked selector for both read/write
  // paths; the canonical Profile never exposes a mutable level reference.
  auto levels = progression.levels;
  const auto level = selected_level(levels, selection);
  if (level == kMaximumLevel) return std::nullopt;
  switch (selection.category) {
    case UpgradeCategory::stat:
      return kStatCosts.at(selection.index).at(level);
    case UpgradeCategory::weapon:
      return kWeaponCosts.at(selection.index).at(level);
    case UpgradeCategory::spell:
      if (selection.index >= kPurchasableSpells) return std::nullopt;
      return kSpellCosts.at(selection.index).at(level);
  }
  throw std::invalid_argument("Invalid upgrade category");
}
PurchaseResult purchase_upgrade(Progression& progression,
                                UpgradeSelection selection) {
  const auto cost = upgrade_cost(progression, selection);
  if (!cost || *cost == 0) return PurchaseResult::unavailable;
  if (*cost > progression.points) return PurchaseResult::insufficient_points;
  // Resolve every fallible condition before committing debit and increment.
  auto& level = selected_level(progression.levels, selection);
  progression.points -= *cost;
  ++level;
  return PurchaseResult::purchased;
}
std::uint32_t mission_reward_points(MissionResultReward result) {
  if (result.rank >= kRewards.size() ||
      result.difficulty >= kRewards.front().size()) {
    throw std::invalid_argument("Invalid classified mission result");
  }
  switch (result.mode) {
    case MissionRewardMode::campaign:
      return kRewards.at(result.rank).at(result.difficulty);
    case MissionRewardMode::stage_replay:
    case MissionRewardMode::concept_mission:
      return 0;
  }
  throw std::invalid_argument("Invalid mission reward mode");
}
bool all_purchasable_upgrades_complete(const PlayerLevels& levels) {
  validate_levels(levels);
  const auto complete = [](std::uint8_t value) {
    return value == kMaximumLevel;
  };
  return std::ranges::all_of(levels.stats, complete) &&
         std::ranges::all_of(levels.weapons, complete) &&
         std::ranges::all_of(
             std::span(levels.spells).first<kPurchasableSpells>(), complete);
}
PlayerLevels merge_checkpoint_levels(const PlayerLevels& permanent,
                                     const PlayerLevels& checkpoint) {
  validate_levels(permanent);
  validate_levels(checkpoint);
  auto result = checkpoint;
  const auto merge = [](auto& target, const auto& source) {
    for (std::size_t i = 0; i < target.size(); ++i) {
      target.at(i) = std::max(target.at(i), source.at(i));
    }
  };
  merge(result.stats, permanent.stats);
  merge(result.weapons, permanent.weapons);
  merge(result.spells, permanent.spells);
  return result;
}
}  // namespace bw::game
