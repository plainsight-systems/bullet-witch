// Mission-owned live health, magic and four persistent weapon resource states.
// Settings are decoded once from original customization; progression/selection
// and WorldClock are stable borrowed authorities and must outlive this owner.
// HUD observes copied values. Actor code receives only action transactions.
// Coordinator begins one immutable world frame, actions commit, then finish
// advances source child recovery/cooldowns once. Paused frames cannot spend.
// Resource commits precede reload/cast presentation; this does not claim hits,
// spell effects or world checkpoint restoration. No generic resource setters.
// Fixed 4/9 arrays, bounded shot loop (at most magazine capacity), no update
// allocation; owner thread only. Design Core I.1 (#ri-explicit), E.5
// (#re-invariant); implementation E.4 (#re-design-invariants) checks before
// mutation. Both phases read performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md). See player-combat-resources.md.
#pragma once
#include "bw/content/formats/customization.hpp"
#include "bw/timing/world_clock.hpp"
#include "game/combat/health_damage.hpp"
#include "game/combat/weapon_resources.hpp"
#include "game/mission/mission_player_state.hpp"

namespace bw::game {
struct HealthRecoverySettings {
  float current_seconds{}, current_delay{}, ceiling_seconds{}, ceiling_delay{};
};
struct WeaponResourceSettings {
  float rate_per_second{};
  unsigned capacity{};
  float recharge_cost{};
};
struct SpellResourceCost {
  float current{}, recoverable{};
};
struct PlayerResourceSettings {
  float maximum_health{};
  bool health_recovery_enabled{};
  std::array<HealthRecoverySettings, 4> health_recovery{};
  std::array<float, 4> magic_recovery_seconds{};
  std::array<WeaponResourceSettings, player_weapon_count> weapons{};
  std::array<std::array<SpellResourceCost, 3>, player_spell_count> spells{};
  static PlayerResourceSettings parse(
      const formats::CustomizationDefinition& tree);
};
struct PlayerResourceSnapshot {
  float health{}, recoverable_health{}, maximum_health{};
  float magic{}, recoverable_magic{}, maximum_magic{};
  unsigned selected_weapon{};
  std::array<WeaponResourceSnapshot, player_weapon_count> weapons{};
  // Frame receipt survives finish; next begin clears even on a paused frame.
  // Consumers use this receipt, never infer recharge from magazine changes.
  std::uint8_t recharged_weapons{};
};
struct ShotResourceResult {
  ShotStatus status{ShotStatus::inactive};
  unsigned shot_count{};
  // A completed resource transaction records the selected weapon and source
  // cooldown debt before each emitted shot. Terminal empty status does not
  // cancel earlier emissions. The shot owner consumes count independently of
  // the caller's animation/recharge status, using first_age - index*interval.
  // Pure copied receipt, no mutable resource or clock authority. Core I.1 and
  // performance MEM.9 keep this explicit and allocation-free in both phases.
  unsigned weapon{};
  float first_shot_age_seconds{}, interval_seconds{};
  unsigned level{};
};
enum class ResourceActionResult : std::uint8_t {
  committed,
  inactive,
  unavailable,
  insufficient_magic,
  magazine_full
};
class PlayerResources;
// Combat receives only this transaction capability. The damage policy has
// already resolved affiliation, body multipliers and invulnerability. No health
// setter or progression/frame authority escapes. Calls outside an admitted
// resource frame do not mutate health or reset recovery delays.
class PlayerDamageResources {
 public:
  HealthDamageResult receive(float magnitude);
  PlayerResourceSnapshot snapshot() const;

 private:
  friend class PlayerResources;
  explicit PlayerDamageResources(PlayerResources& owner) : owner_(&owner) {}
  PlayerResources* owner_;
};
// World damage outcomes receive only the original defeat-notification
// capability. It raises recoverable magic, never current magic or progression.
// The world delivers each accepted lethal transition once; calls require an
// admitted frame but remain valid in death states, as in original MagicUser
// dispatch. Design I.1/E.4 and MEM.9: explicit borrowed authority, validated
// scalar input, fixed state and no allocation. Source
// 6867b0/5a4d90/60aa70/612f20.
class PlayerDefeatResources {
 public:
  ResourceActionResult receive(float authored_amount);

 private:
  friend class PlayerResources;
  explicit PlayerDefeatResources(PlayerResources& owner) : owner_(&owner) {}
  PlayerResources* owner_;
};
// Borrowed action capability; no clock/frame control, selection or setters.
// Calls are synchronous; retain only while the resource owner exists.
class PlayerActionResources {
 public:
  ShotResourceResult try_fire();
  ResourceActionResult try_reload();
  ResourceActionResult commit_spell(PlayerSpell spell);

 private:
  friend class PlayerResources;
  explicit PlayerActionResources(PlayerResources& owner) : owner_(&owner) {}
  PlayerResources* owner_;
};
class PlayerResources {
 public:
  PlayerResources(const PlayerResourceSettings& settings,
                  const MissionPlayerState& mission,
                  const world::WorldClock& clock,
                  std::optional<CheckpointPlayer> checkpoint = {});
  ~PlayerResources() = default;
  PlayerResources(const PlayerResources&) = delete;
  PlayerResources& operator=(const PlayerResources&) = delete;
  PlayerResources(PlayerResources&&) = delete;
  PlayerResources& operator=(PlayerResources&&) = delete;
  PlayerActionResources actions() { return PlayerActionResources(*this); }
  PlayerDamageResources damage_receiver() {
    return PlayerDamageResources(*this);
  }
  PlayerDefeatResources defeat_receiver() {
    return PlayerDefeatResources(*this);
  }
  PlayerResourceSnapshot snapshot() const;
  SpellResourceCost spell_cost(PlayerSpell spell) const;
  // Read-only affordability is valid while paused; never starts an action.
  bool can_afford_spell(PlayerSpell spell) const;
  // Coordinator only: rejects foreign/repeated/reentrant frame before writes.
  void begin_frame(const world::WorldFrame& frame);
  // Normal live-player recovery dispatch; unusual damage states need their
  // own traced integration before admission. Does not accept a second delta.
  void finish_frame();

 private:
  friend class PlayerActionResources;
  friend class PlayerDamageResources;
  friend class PlayerDefeatResources;
  ResourceActionResult receive_defeat(float authored_amount);
  HealthDamageResult receive_damage(float magnitude);
  ShotResourceResult fire();
  ResourceActionResult reload();
  ResourceActionResult spend_spell(PlayerSpell spell);
  bool actions_admitted() const noexcept;
  const PlayerResourceSettings settings_;
  const MissionPlayerState& mission_;
  const world::WorldClock& clock_;
  std::array<WeaponResources, player_weapon_count> weapons_;
  float health_{}, health_ceiling_{}, magic_{}, magic_ceiling_{},
      magic_floor_{};
  float health_delay_{}, health_ceiling_delay_{};
  std::uint64_t last_sequence_{};
  float frame_delta_{};
  bool frame_open_{}, simulation_admitted_{};
  bool health_recovery_active_{true};
  std::uint8_t recharged_weapons_{};
};
}  // namespace bw::game
