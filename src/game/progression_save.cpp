// Bounded original Record and checkpoint mission-field adaptation. Unknown
// bytes survive; complete candidate validation precedes writes. The checkpoint
// reader does not deserialize world objects or make Continue executable.
// Algorithms/ownership in progression_save.hpp; constant field work, no heap.
// Design I.30/C.9; implementation E.4 (#re-design-invariants) retains invalid
// input unchanged. Both-phase MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) retains fixed storage.
#include "game/progression_save.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace bw::game {
namespace {
constexpr std::size_t kRecordBytes = 992, kPointsOffset = 0x14;
constexpr std::size_t kBookOffset = 0x1c, kFirstSpellBookBit = 9;
constexpr unsigned kByteBits = 8, kByteMask = 255;
constexpr std::size_t kMissionSizeField = 0x10, kMissionOffsetField = 0x14;
constexpr std::size_t kMissionBytes = 0x3a4, kHeaderBytes = 0x30;
constexpr std::size_t kHealthOffset = 0x324, kMagicOffset = 0x328;
constexpr std::size_t kCheckpointBytes = 307200;
std::uint32_t read_word(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t result{};
  for (unsigned index = 0; index < sizeof(result); ++index) {
    result |= std::to_integer<std::uint32_t>(bytes[offset + index])
              << (index * kByteBits);
  }
  return result;
}
void write_word(std::span<std::byte> bytes, std::size_t offset,
                std::uint32_t value) {
  for (unsigned index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * kByteBits)) & kByteMask);
  }
}
void require_record(std::size_t size) {
  if (size != kRecordBytes) throw std::invalid_argument("Invalid Record size");
}
PlayerLevels read_levels(std::span<const std::byte> bytes) {
  PlayerLevels levels;
  std::size_t offset = 2;
  const auto read = [&bytes, &offset](auto& values) {
    for (auto& value : values) {
      value = std::to_integer<std::uint8_t>(bytes[offset++]);
    }
  };
  read(levels.stats);
  read(levels.weapons);
  read(levels.spells);
  validate_levels(levels);
  return levels;
}
}  // namespace
Progression decode_record_progression(std::span<const std::byte> record) {
  require_record(record.size());
  Progression result{read_levels(record), read_word(record, kPointsOffset)};
  validate_progression(result);
  return result;
}
void encode_record_progression(std::span<std::byte> record,
                               const Progression& progression) {
  require_record(record.size());
  validate_progression(progression);
  std::size_t offset = 2;
  const auto write = [&record, &offset](const auto& values) {
    for (auto value : values) record[offset++] = static_cast<std::byte>(value);
  };
  write(progression.levels.stats);
  write(progression.levels.weapons);
  write(progression.levels.spells);
  write_word(record, kPointsOffset, progression.points);
}
void mark_spell_acquired(std::span<std::byte> record, unsigned spell) {
  require_record(record.size());
  constexpr unsigned kPurchasableSpells = 6;
  if (spell >= kPurchasableSpells) {
    throw std::invalid_argument("Invalid purchasable spell");
  }
  write_word(
      record, kBookOffset,
      read_word(record, kBookOffset) | (1U << (kFirstSpellBookBit + spell)));
}
std::optional<CheckpointPlayer> decode_checkpoint_player(
    std::span<const std::byte> checkpoint) {
  constexpr std::uint32_t kVersion = 0x10001;
  if (checkpoint.size() != kCheckpointBytes ||
      read_word(checkpoint, 0) != kVersion) {
    throw std::invalid_argument("Invalid checkpoint envelope");
  }
  const auto used = read_word(checkpoint, 4);
  const auto size = read_word(checkpoint, kMissionSizeField);
  const auto offset = read_word(checkpoint, kMissionOffsetField);
  if (used < kHeaderBytes || used > checkpoint.size()) {
    throw std::invalid_argument("Invalid checkpoint used length");
  }
  if (size == 0) return std::nullopt;
  if (size != kMissionBytes || offset < kHeaderBytes || offset > used ||
      size > used - offset) {
    throw std::invalid_argument("Invalid checkpoint mission section");
  }
  const auto mission = checkpoint.subspan(offset, size);
  CheckpointPlayer result{
      read_levels(mission), std::to_integer<unsigned>(mission[1]),
      std::bit_cast<float>(read_word(mission, kHealthOffset)),
      std::bit_cast<float>(read_word(mission, kMagicOffset))};
  if (result.selected_weapon >= player_weapon_count ||
      !std::isfinite(result.health) || !std::isfinite(result.magic)) {
    throw std::invalid_argument("Invalid checkpoint player fields");
  }
  return result;
}
}  // namespace bw::game
