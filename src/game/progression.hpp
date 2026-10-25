// Permanent player progression values and recovered upgrade policy. This
// decomposes the original Record/power-up service; no clocks, actors or I/O.
// Values are owned snapshots. Profile is the permanent authority; mutations
// validate a candidate before publication. Invalid fields throw before
// mutation; unavailable/unaffordable purchases return a result with unchanged
// state. Algorithm: validate levels -> resolve bounded cost -> check wallet ->
// deduct and increment atomically. Mission copies do not write back implicitly.
// Costs are fixed 2/4/9 inline arrays, O(1) purchase, no heap allocations or
// synchronization. Owner-thread calls only. Design: cpp-guidelines C.9
// (#rc-private) keeps authority behind Profile operations; I.30
// (#ri-encapsulate) confines compatibility mapping to progression_save;
// cpp-performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keeps gameplay state and queries fixed-size. See player-progression.md.
// Implementation consultation: Core E.4 (#re-design-invariants) preserves
// state on failed validation; MEM.9 was reread for bounded inline operations.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace bw::game {
inline constexpr std::size_t player_stat_count = 2;
inline constexpr std::size_t player_weapon_count = 4;
inline constexpr std::size_t player_spell_count = 9;
inline constexpr std::uint32_t maximum_upgrade_points = 99999;
enum class PlayerSpell : std::uint8_t {
  elemental_shot,
  ancient_wall,
  rose_bomb,
  will_power,
  sacrifice,
  raven_panic,
  thunder,
  tornado,
  meteor
};
struct PlayerLevels {
  std::array<std::uint8_t, player_stat_count> stats{1, 1};
  std::array<std::uint8_t, player_weapon_count> weapons{1, 0, 0, 0};
  std::array<std::uint8_t, player_spell_count> spells{0, 1, 0, 0, 1,
                                                      0, 0, 0, 0};
  bool operator==(const PlayerLevels&) const = default;
};
struct Progression {
  PlayerLevels levels;
  std::uint32_t points{};
  bool operator==(const Progression&) const = default;
};
enum class UpgradeCategory : std::uint8_t { stat, weapon, spell };
struct UpgradeSelection {
  UpgradeCategory category{UpgradeCategory::stat};
  unsigned index{};
};
enum class PurchaseResult : std::uint8_t {
  purchased,
  unavailable,
  insufficient_points
};
enum class MissionRewardMode : std::uint8_t {
  campaign,
  stage_replay,
  concept_mission
};
struct MissionResultReward {
  // Already classified original result rank, not raw score inputs.
  unsigned rank{};
  unsigned difficulty{};
  MissionRewardMode mode{MissionRewardMode::campaign};
};
std::uint32_t mission_reward_points(MissionResultReward result);
bool all_purchasable_upgrades_complete(const PlayerLevels& levels);
void validate_levels(const PlayerLevels& levels);
void validate_progression(const Progression& progression);
// Returns no cost for capped or non-purchasable abilities. Invalid IDs throw.
std::optional<std::uint32_t> upgrade_cost(const Progression& progression,
                                          UpgradeSelection selection);
PurchaseResult purchase_upgrade(Progression& progression,
                                UpgradeSelection selection);
// Checkpoint continuation keeps the larger source byte for every level; wallet
// remains permanent and is never restored from checkpoint-local player data.
PlayerLevels merge_checkpoint_levels(const PlayerLevels& permanent,
                                     const PlayerLevels& checkpoint);
}  // namespace bw::game
