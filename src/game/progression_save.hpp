// Original save-field codec, isolated from gameplay policy and platform I/O.
// Inputs borrow exact Record bytes or a validated checkpoint section envelope;
// outputs own typed values. Encoders validate before touching mapped fields and
// preserve all unknown bytes. No casts to packed structs or borrowed outputs.
// Algorithm: verify sizes/bounds -> decode fixed little-endian fields ->
// validate levels; encoding performs the inverse only for known fields.
// Checkpoint extraction reads the mission section, never invents world records.
// Cost O(15) fields, fixed value storage, no allocation; owner-thread only.
// Design: Core I.30 (#ri-encapsulate) isolates compatibility; C.9 (#rc-private)
// keeps mutable permanent state inside Profile; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) fixes query storage.
// Implementation consultation: Core E.4 (#re-design-invariants) preserves
// state on failed validation; MEM.9 was reread for bounded inline operations.
#pragma once
#include <optional>
#include <span>

#include "game/progression.hpp"

namespace bw::game {
Progression decode_record_progression(std::span<const std::byte> record);
void encode_record_progression(std::span<std::byte> record,
                               const Progression& progression);
// Original first acquisition of a purchasable spell opens its tutorial entry.
void mark_spell_acquired(std::span<std::byte> record, unsigned spell);
struct CheckpointPlayer {
  PlayerLevels levels;
  unsigned selected_weapon{};
  float health{};
  float magic{};
};
// No mission section returns nullopt; malformed present fields throw. This is
// player data inspection, not permission to restore an incomplete world.
std::optional<CheckpointPlayer> decode_checkpoint_player(
    std::span<const std::byte> checkpoint);
}  // namespace bw::game
