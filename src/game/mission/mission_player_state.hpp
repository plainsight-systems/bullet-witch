// Stable mission authority: live resources and script consumers borrow this
// identity through teardown. Copy/move are forbidden; observation returns const
// levels, while equipment and script restrictions retain separate commands.
// Design/implementation Core C.21 (#rc-five) and performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) enforce
// unique authority without duplicating state; stable borrowers also forbid
// move. Mission-owned effective player progression and selected equipment.
// Decomposes the original mission/player application boundary, independently
// from Record. Entry inherits permanent levels or merges a decoded checkpoint.
// Development concept entry has an explicit, nonpersistent profile origin.
// Mission masks disable capabilities without changing learned levels. No combat
// simulation, clocks, GPU state or I/O; full world restoration remains outside
// this type. Algorithm: validate entry -> establish effective levels -> repair
// selection to an acquired weapon -> answer admission; caller commits selection
// only after its resource/action transaction succeeds. Rejections retain
// previous state. Fixed arrays and bounded four-weapon scan, no allocations.
// Single owner thread. Design: Core C.9 (#rc-private) enforces authority, I.30
// (#ri-encapsulate) isolates source selectors; performance MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) keeps all queries
// inline and bounded. Implementation consultation: Core E.4
// (#re-design-invariants) preserves state on failed validation; MEM.9 was
// reread for bounded inline operations.
#pragma once
#include "game/profile/progression_save.hpp"

namespace bw::game {
enum class ConceptProfile : std::uint8_t { saved, development_completed };
enum class ProgressionOrigin : std::uint8_t {
  saved_profile,
  checkpoint,
  development_completed
};
struct MissionPlayerEntry {
  Progression progression;
  std::optional<CheckpointPlayer> checkpoint;
  bool concept_mission{};
  ConceptProfile concept_profile{ConceptProfile::saved};
};
// Explicit authored development data, not an inferred all-max completion save.
Progression development_completed_progression();
class MissionPlayerState {
 public:
  explicit MissionPlayerState(const MissionPlayerEntry& entry);
  ~MissionPlayerState() = default;
  MissionPlayerState(const MissionPlayerState&) = delete;
  MissionPlayerState& operator=(const MissionPlayerState&) = delete;
  MissionPlayerState(MissionPlayerState&&) = delete;
  MissionPlayerState& operator=(MissionPlayerState&&) = delete;
  const PlayerLevels& levels() const noexcept { return levels_; }
  ProgressionOrigin origin() const noexcept { return origin_; }
  unsigned selected_weapon() const noexcept { return selected_weapon_; }
  bool weapon_available(unsigned weapon) const;
  unsigned next_weapon(int direction = 1) const;
  void select_weapon(unsigned weapon);
  bool spell_available(unsigned spell) const;
  // Independent original owners: updating the script mask must preserve the
  // context mask (and vice versa). Availability consumes their union.
  void set_script_disabled_spells(std::uint32_t mask);
  void set_context_disabled_spells(std::uint32_t mask);

 private:
  PlayerLevels levels_;
  ProgressionOrigin origin_{ProgressionOrigin::saved_profile};
  unsigned selected_weapon_{};
  std::uint32_t script_disabled_spells_{};
  std::uint32_t context_disabled_spells_{};
};
}  // namespace bw::game
