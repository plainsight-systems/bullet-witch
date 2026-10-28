// Decode source placement fields into immutable actor admission values. Numeric
// defaults follow the placement producer, not a generalized AI fallback. Core
// E.4 (#re-design-invariants) validates the complete candidate; performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps all table
// access outside frames. Both implementation corpora read.
#include "npc_combat_metadata.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace bw::game {
namespace {
constexpr std::size_t kMoveRange = 7, kNearRange = 8, kMiddleRange = 9,
                      kFarRange = 10, kMode = 12, kRate = 13, kView = 17,
                      kFarView = 18, kCloseView = 19, kVoice = 24;
constexpr float kPlacementRange = 10;
constexpr std::uint32_t kCivilian = 8;
char lower(char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A'))
                                      : value;
}
std::uint32_t affiliation(const content::PlacementArchetype& actor) {
  std::uint32_t value = 2;
  if (actor.model.size() >= 2) {
    const auto first = lower(actor.model[0]), second = lower(actor.model[1]);
    if (first == 'p' && second == 'w') value = 1;
    if (first == 'h' && (second == 'm' || second == 'w')) value = kCivilian;
    if (first == 'h' && second == 'r') value = 4;
  }
  const auto flags = static_cast<std::uint32_t>(actor.actor_flags);
  constexpr std::uint32_t kEnemyOverride = 16;
  if ((flags & 4U) != 0) return 4;
  if ((flags & kCivilian) != 0) return kCivilian;
  if ((flags & kEnemyOverride) != 0) return 2;
  return value;
}
}  // namespace
NpcCombatMetadata prepare_npc_combat_metadata(
    const content::MissionPlacementDefinition& definition,
    const content::PlacementArchetype& actor, std::int32_t group) {
  const auto& table = definition.original_table();
  const auto section = table.find("chara");
  if (!section || table.integer({*section, actor.source_row, 0}) != actor.id ||
      table.text({*section, actor.source_row, 1}) != actor.model) {
    throw std::invalid_argument("NPC admission archetype does not match table");
  }
  const auto number = [&](std::size_t column) {
    const auto value =
        table.number({*section, actor.source_row, column}).value_or(0);
    if (!std::isfinite(value)) {
      throw std::invalid_argument("Nonfinite NPC admission field");
    }
    return value;
  };
  const auto range = [&](std::size_t column) {
    const auto value = number(column);
    return value == 0 ? kPlacementRange : value;
  };
  NpcCombatMetadata result;
  result.ai.script.group_number = group;
  result.ai.script.move_range = range(kMoveRange);
  result.ai.script.attack_range_near = range(kNearRange);
  result.ai.script.attack_range_middle = range(kMiddleRange);
  result.ai.script.attack_range_far = range(kFarRange);
  if (const auto path = table.find("path"); path && !actor.path.empty()) {
    for (std::size_t row = 0; row < table.sections().at(*path).rows; ++row) {
      if (table.text({*path, row, 0}) == actor.path) {
        result.ai.script.loop_flag = table.integer({*path, row, 1}).value_or(0);
        break;
      }
    }
  }
  result.ai.view_degrees = number(kView);
  if (result.ai.view_degrees <= 0) {
    result.ai.view_degrees = NpcActorAiSettings::kViewDegrees;
  }
  result.ai.far_radius = number(kFarView);
  if (result.ai.far_radius <= 0) {
    result.ai.far_radius = NpcActorAiSettings::kFarView;
  }
  result.ai.close_radius = number(kCloseView);
  if (result.ai.close_radius <= 0) {
    result.ai.close_radius = NpcActorAiSettings::kNearRange;
  }
  result.movement_rate_scale = number(kRate);
  result.ai_mode =
      table.integer({*section, actor.source_row, kMode}).value_or(0);
  const auto voice = number(kVoice);
  // Native float-to-int truncates before profile lookup. Unsupported groups
  // resolve to the original silent group through the shared audio policy.
  if (static_cast<double>(voice) < std::numeric_limits<std::int32_t>::min() ||
      static_cast<double>(voice) > std::numeric_limits<std::int32_t>::max()) {
    throw std::invalid_argument("NPC voice group conversion overflows");
  }
  result.audio = original_character_audio_profile(
      actor.model, static_cast<std::uint32_t>(actor.actor_flags),
      static_cast<std::uint32_t>(static_cast<std::int32_t>(voice)));
  result.affiliation = affiliation(actor);
  result.leader =
      !actor.behavior.empty() && lower(actor.behavior.front()) == 'l';
  return result;
}
std::int32_t npc_target_model(std::uint32_t value) noexcept {
  constexpr std::uint32_t kTransformed = 0x8000;
  constexpr std::array<std::uint32_t, 11> kCategories{
      1, 2, 4, 8, 0x102, 0x202, 0x402, 0x802, 0x4002, 0x1002, 0x2002};
  if ((value & kTransformed) != 0) {
    return static_cast<std::int32_t>(kCategories.size());
  }
  for (std::size_t i = 0; i < kCategories.size(); ++i) {
    if (std::span{kCategories}[i] == value) return static_cast<std::int32_t>(i);
  }
  return -1;
}
}  // namespace bw::game
