// In-process progression, mission entry and original-save contract tests.
// Owned byte fixtures exercise publication, rejection and entry isolation; no
// game assets, filesystem, subprocesses or simulated combat are involved.
// Fixed values plus one bounded checkpoint buffer; O(save size) comparisons.
// Design and implementation consulted Core E.4 (#re-design-invariants): failed
// operations preserve valid state. Performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps fixture ownership in containers
// without custom lifetime machinery. Tests do not establish full world restore.
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "game/mission/mission_player_state.hpp"
#include "game/profile/profile.hpp"

namespace {
using bw::game::all_purchasable_upgrades_complete;
using bw::game::CheckpointPlayer;
using bw::game::ConceptProfile;
using bw::game::decode_checkpoint_player;
using bw::game::decode_record_progression;
using bw::game::development_completed_progression;
using bw::game::encode_record_progression;
using bw::game::mission_reward_points;
using bw::game::MissionPlayerEntry;
using bw::game::MissionPlayerState;
using bw::game::MissionRewardMode;
using bw::game::player_spell_count;
using bw::game::PlayerLevels;
using bw::game::Profile;
using bw::game::Progression;
using bw::game::ProgressionOrigin;
using bw::game::purchase_upgrade;
using bw::game::PurchaseResult;
using bw::game::upgrade_cost;
using bw::game::UpgradeCategory;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template <class Operation>
void require_rejected(Operation operation, const char* message) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::logic_error&) {
    rejected = true;
  }
  require(rejected, message);
}
void word(std::span<std::byte> bytes, std::size_t offset, std::uint32_t value) {
  for (unsigned index = 0; index < 4; ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 255U);
  }
}

void defaults_and_purchases() {
  const Progression fresh;
  require(fresh.levels.stats == std::array<std::uint8_t, 2>{1, 1} &&
              fresh.levels.weapons == std::array<std::uint8_t, 4>{1, 0, 0, 0} &&
              fresh.levels.spells ==
                  std::array<std::uint8_t, 9>{0, 1, 0, 0, 1, 0, 0, 0, 0} &&
              fresh.points == 0,
          "fresh campaign uses recovered Record defaults");
  Progression progression = fresh;
  require(purchase_upgrade(progression, {UpgradeCategory::weapon, 1}) ==
                  PurchaseResult::insufficient_points &&
              progression == fresh,
          "unaffordable acquisition changes neither level nor wallet");
  progression.points = 40;
  require(upgrade_cost(progression, {UpgradeCategory::weapon, 1}) == 40 &&
              purchase_upgrade(progression, {UpgradeCategory::weapon, 1}) ==
                  PurchaseResult::purchased &&
              progression.levels.weapons[1] == 1 && progression.points == 0 &&
              upgrade_cost(progression, {UpgradeCategory::weapon, 1}) == 150,
          "weapon acquisition and next upgrade have distinct original costs");
  progression.points = 900;
  progression.levels.weapons[3] = 2;
  require(purchase_upgrade(progression, {UpgradeCategory::weapon, 3}) ==
                  PurchaseResult::purchased &&
              progression.levels.weapons[3] == 3 && progression.points == 0,
          "final Gatling purchase consumes exactly its cost");
  const auto capped = progression;
  require(purchase_upgrade(progression, {UpgradeCategory::weapon, 3}) ==
                  PurchaseResult::unavailable &&
              progression == capped,
          "capped upgrade is unavailable without mutation");
  require_rejected(
      [&] {
        (void)purchase_upgrade(progression, {UpgradeCategory::weapon, 4});
      },
      "invalid weapon selector is rejected");
  require(progression == capped, "invalid selector leaves progression intact");
  progression.points = 50;
  require(purchase_upgrade(progression, {UpgradeCategory::stat, 0}) ==
                  PurchaseResult::purchased &&
              progression.levels.stats[0] == 2 && progression.points == 0,
          "stat upgrade follows the separate stat cost table");
  progression.points = 25;
  require(purchase_upgrade(progression, {UpgradeCategory::spell, 2}) ==
                  PurchaseResult::purchased &&
              progression.levels.spells[2] == 1 && progression.points == 0,
          "purchasable spell acquisition consumes its own original cost");
}

void rewards_and_completion() {
  require(mission_reward_points({0, 4, MissionRewardMode::campaign}) == 750 &&
              mission_reward_points({6, 0, MissionRewardMode::campaign}) == 70,
          "classified result rank and difficulty select original rewards");
  require(
      mission_reward_points({0, 4, MissionRewardMode::concept_mission}) == 0 &&
          mission_reward_points({0, 4, MissionRewardMode::stage_replay}) == 0,
      "concept and stage replay results do not award campaign currency");
  require_rejected([] { (void)mission_reward_points({7, 0}); },
                   "rank outside original result table is rejected");
  require_rejected([] { (void)mission_reward_points({0, 5}); },
                   "difficulty outside original result table is rejected");
  Progression progression;
  progression.levels.stats.fill(3);
  progression.levels.weapons.fill(3);
  std::fill_n(progression.levels.spells.begin(), 6, 3);
  require(all_purchasable_upgrades_complete(progression.levels),
          "all-upgrades predicate excludes the three story-acquired spells");
  const auto before = progression;
  for (unsigned spell = 6; spell < player_spell_count; ++spell) {
    require(purchase_upgrade(progression, {UpgradeCategory::spell, spell}) ==
                PurchaseResult::unavailable,
            "story spells cannot be acquired through purchases");
  }
  require(progression == before,
          "story purchase attempts preserve progression");
  progression.levels.spells[5] = 2;
  require(!all_purchasable_upgrades_complete(progression.levels),
          "the sixth purchasable spell still participates in completion");
}

void mission_entry_and_merge() {
  MissionPlayerEntry entry;
  const MissionPlayerState fresh(entry);
  require(fresh.selected_weapon() == 0 && fresh.next_weapon() == 0 &&
              fresh.origin() == ProgressionOrigin::saved_profile &&
              fresh.levels() == PlayerLevels{},
          "fresh campaign starts with its one acquired weapon");
  entry.progression.levels.stats = {3, 1};
  entry.progression.levels.weapons = {1, 2, 0, 0};
  entry.progression.levels.spells[6] = 3;
  entry.progression.points = 123;
  CheckpointPlayer checkpoint;
  checkpoint.levels.stats = {1, 2};
  checkpoint.levels.weapons = {1, 1, 3, 0};
  checkpoint.levels.spells[2] = 2;
  checkpoint.selected_weapon = 2;
  entry.checkpoint = checkpoint;
  const MissionPlayerState resumed(entry);
  require(
      resumed.origin() == ProgressionOrigin::checkpoint &&
          resumed.selected_weapon() == 2 &&
          resumed.levels().stats == std::array<std::uint8_t, 2>{3, 2} &&
          resumed.levels().weapons == std::array<std::uint8_t, 4>{1, 2, 3, 0} &&
          resumed.levels().spells[2] == 2 && resumed.levels().spells[6] == 1 &&
          entry.progression.points == 123 &&
          entry.progression.levels.spells[6] == 3,
      "checkpoint uses per-field maxima and normalizes story levels only live");
  entry.concept_mission = true;
  const MissionPlayerState saved_concept(entry);
  require(saved_concept.origin() == ProgressionOrigin::saved_profile &&
              saved_concept.selected_weapon() == 0 &&
              !saved_concept.weapon_available(2) &&
              saved_concept.levels().stats == entry.progression.levels.stats &&
              saved_concept.levels().spells[2] == 0,
          "saved concept inherits permanent progression but ignores campaign "
          "checkpoint");
  const auto permanent_before = entry.progression;
  entry.concept_profile = ConceptProfile::development_completed;
  const MissionPlayerState development(entry);
  require(development.origin() == ProgressionOrigin::development_completed &&
              development.levels().weapons ==
                  std::array<std::uint8_t, 4>{1, 1, 1, 1} &&
              std::ranges::all_of(development.levels().spells,
                                  [](auto level) { return level == 1; }) &&
              entry.progression == permanent_before,
          "explicit development entry supplies minimum learned levels without "
          "writeback");
  entry.concept_mission = false;
  entry.checkpoint.reset();
  const MissionPlayerState campaign(entry);
  require(campaign.origin() == ProgressionOrigin::saved_profile &&
              !campaign.weapon_available(2),
          "development concept option cannot unlock campaign equipment");
}

void selection_and_restrictions() {
  MissionPlayerEntry entry;
  entry.progression.levels.weapons = {0, 0, 2, 0};
  MissionPlayerState player(entry);
  require(player.selected_weapon() == 2 && player.next_weapon() == 2,
          "fresh selection finds the first acquired weapon");
  require_rejected([&] { player.select_weapon(1); },
                   "unacquired weapon selection is rejected");
  require_rejected([&] { player.select_weapon(4); },
                   "out-of-range weapon selection is rejected");
  require(player.selected_weapon() == 2,
          "rejected selection preserves the current weapon");
  entry.progression.levels.weapons.fill(0);
  const MissionPlayerState repaired(entry);
  require(repaired.weapon_available(0) && repaired.selected_weapon() == 0 &&
              entry.progression.levels.weapons[0] == 0,
          "no-weapon repair grants a live fallback without editing permanent "
          "levels");
  entry.progression = development_completed_progression();
  MissionPlayerState restricted(entry);
  restricted.select_weapon(3);
  require(restricted.next_weapon() == 0,
          "cycling wraps to the next acquired weapon");
  restricted.set_script_disabled_spells(64);
  require(restricted.spell_available(6) && !restricted.spell_available(7),
          "script bit64 disables Tornado rather than Thunder");
  restricted.set_script_disabled_spells(128);
  restricted.set_context_disabled_spells(256);
  require(
      !restricted.spell_available(6) && restricted.spell_available(7) &&
          !restricted.spell_available(8),
      "script and contextual masks combine without swapping the last spells");
  restricted.set_script_disabled_spells(0);
  require(restricted.spell_available(6) && !restricted.spell_available(8),
          "clearing the script mask preserves the contextual restriction");
  restricted.set_script_disabled_spells(128);
  restricted.set_context_disabled_spells(0);
  require(!restricted.spell_available(6) && restricted.spell_available(8),
          "clearing the context mask preserves the script restriction");
  restricted.set_context_disabled_spells(256);
  require_rejected([&] { restricted.set_context_disabled_spells(0x200); },
                   "unknown contextual mask bits are rejected");
  require(!restricted.spell_available(6) && !restricted.spell_available(8),
          "invalid context mask leaves both owners intact");
  restricted.set_script_disabled_spells(0x1c8);
  require(!restricted.spell_available(3) && !restricted.spell_available(6) &&
              !restricted.spell_available(7) &&
              !restricted.spell_available(8) && restricted.spell_available(1) &&
              restricted.levels().spells[3] == 1,
          "One Shot Kill restrictions do not erase learned abilities");
  require_rejected([&] { restricted.set_script_disabled_spells(0x200); },
                   "unknown spell mask bits are rejected");
  require(!restricted.spell_available(3),
          "invalid mask retains the prior restrictions");
  restricted.set_script_disabled_spells(0);
  require(restricted.spell_available(3) && !restricted.spell_available(8),
          "removing mission restrictions preserves context and learned levels");
  restricted.set_context_disabled_spells(0);
  require(restricted.spell_available(8),
          "removing the last restriction exposes the retained learned ability");
}

void record_codec() {
  std::array<std::byte, 992> record{};
  record.fill(std::byte{0xa5});
  const auto original = record;
  Progression progression;
  progression.levels.weapons = {1, 2, 3, 0};
  progression.points = 0x1234;
  encode_record_progression(record, progression);
  require(decode_record_progression(record) == progression &&
              record[0x14] == std::byte{0x34} &&
              record[0x15] == std::byte{0x12},
          "Record progression round trip uses original little-endian wallet");
  for (std::size_t index = 0; index < record.size(); ++index) {
    if ((index >= 2 && index <= 16) || (index >= 0x14 && index <= 0x17)) {
      continue;
    }
    require(record.at(index) == original.at(index),
            "progression encoding preserves every unmapped Record byte");
  }
  const auto valid = record;
  progression.levels.weapons[0] = 4;
  require_rejected([&] { encode_record_progression(record, progression); },
                   "invalid progression cannot be encoded");
  require(record == valid, "failed encoding publishes no partial field writes");
  require_rejected(
      [&] { (void)decode_record_progression(std::span(record).first(991)); },
      "truncated Record is rejected");
  record[4] = std::byte{4};
  require_rejected([&] { (void)decode_record_progression(record); },
                   "invalid weapon level in original bytes is rejected");
  record = valid;
  word(record, 0x14, 100000);
  require_rejected([&] { (void)decode_record_progression(record); },
                   "wallet beyond the original cap is rejected");
}

void checkpoint_codec() {
  std::vector<std::byte> bytes(307200);
  word(bytes, 0, 0x10001);
  word(bytes, 4, 0x900);
  require(!decode_checkpoint_player(bytes),
          "base-only checkpoint has no player snapshot");
  // Independent byte fixture uses a nondefault section offset, with different
  // base bytes, to catch accidental whole-file/base-relative interpretation.
  constexpr std::size_t mission = 0x540;
  word(bytes, 8, 0x500);
  word(bytes, 12, 0x30);
  word(bytes, 16, 0x3a4);
  word(bytes, 20, mission);
  bytes[0x31] = std::byte{1};
  bytes[mission + 1] = std::byte{3};
  bytes[mission + 2] = std::byte{2};
  bytes[mission + 3] = std::byte{1};
  bytes[mission + 4] = std::byte{1};
  bytes[mission + 7] = std::byte{2};
  bytes[mission + 14] = std::byte{1};
  word(bytes, mission + 0x324, std::bit_cast<std::uint32_t>(125.5F));
  word(bytes, mission + 0x328, std::bit_cast<std::uint32_t>(40.25F));
  const auto snapshot = decode_checkpoint_player(bytes);
  require(
      snapshot && snapshot->selected_weapon == 3 &&
          snapshot->levels.stats[0] == 2 && snapshot->levels.weapons[3] == 2 &&
          snapshot->levels.spells[6] == 1 && snapshot->health == 125.5F &&
          snapshot->magic == 40.25F,
      "checkpoint player fields are relative to the declared mission section");
  word(bytes, 20, 0x8ff);
  require_rejected([&] { (void)decode_checkpoint_player(bytes); },
                   "mission section extending beyond used bytes is rejected");
  word(bytes, 20, mission);
  word(bytes, mission + 0x324,
       std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity()));
  require_rejected([&] { (void)decode_checkpoint_player(bytes); },
                   "nonfinite health cannot enter live player state");
}

class ProgressionStorage final : public bw::platform::SaveStorage {
 public:
  bw::platform::SaveRead read(std::string_view name, std::size_t) override {
    const auto found = files.find(std::string(name));
    if (found == files.end()) return {bw::platform::SaveStatus::missing, {}};
    return {bw::platform::SaveStatus::ok, found->second};
  }
  bw::platform::SaveStatus write(std::string_view name,
                                 std::span<const std::byte> bytes) override {
    files[std::string(name)] = {bytes.begin(), bytes.end()};
    return bw::platform::SaveStatus::ok;
  }
  std::map<std::string, std::vector<std::byte>> files;
};
void profile_publication() {
  ProgressionStorage storage;
  Profile profile;
  require(profile.load(storage).status == bw::platform::SaveStatus::ok &&
              profile.progression() == Progression{},
          "Profile publishes original fresh progression");
  require(
      profile.award_result({0, 4}) == 750 &&
          profile.purchase({UpgradeCategory::spell, 2}) ==
              PurchaseResult::purchased &&
          profile.progression().points == 725 &&
          (std::to_integer<unsigned>(profile.record_bytes()[0x1d]) & 8U) != 0,
      "successful spell purchase publishes wallet level and original tutorial "
      "bit");
  require(profile.save(storage).status == bw::platform::SaveStatus::ok,
          "progression persists through the existing storage capability");
  Profile restored;
  require(restored.load(storage).status == bw::platform::SaveStatus::ok &&
              restored.progression() == profile.progression() &&
              !restored.dirty(),
          "save reload preserves authoritative levels and currency");
  const auto saved = restored.progression();
  require(
      restored.award_result({0, 4, MissionRewardMode::concept_mission}) == 0 &&
          restored.progression() == saved && !restored.dirty(),
      "concept result leaves permanent progression clean");
  auto& record = storage.files.at("BulletWitchRecord");
  record[900] = std::byte{0x5a};
  word(record, 0x14, 99990);
  require(restored.load(storage).status == bw::platform::SaveStatus::ok &&
              restored.award_result({0, 4}) == 9 &&
              restored.progression().points == 99999,
          "result publication reports only currency credited before the cap");
  require(restored.save(storage).status == bw::platform::SaveStatus::ok &&
              storage.files.at("BulletWitchRecord")[900] == std::byte{0x5a},
          "progression save preserves unrelated original Record contents");
  const auto before_bad_load = restored.progression();
  storage.files.at("BulletWitchRecord")[4] = std::byte{4};
  require(restored.load(storage).status == bw::platform::SaveStatus::invalid &&
              restored.progression() == before_bad_load,
          "invalid imported progression leaves active Profile unchanged");
}
}  // namespace

void progression_contracts() {
  defaults_and_purchases();
  rewards_and_completion();
  mission_entry_and_merge();
  selection_and_restrictions();
  record_codec();
  checkpoint_codec();
  profile_publication();
}
