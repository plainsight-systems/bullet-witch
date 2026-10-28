// Per-weapon source magazine and cadence state, shared by player and NPC
// receivers. The actor owns this value; no clock, input, resource selection,
// magic payment, animation or projectile authority enters it. Construct once
// from validated capacity/rate, accept shots before the owning admitted frame's
// child update, then advance that admitted delta exactly once. Retain one
// negative overshoot and discard idle debt, matching the original shooter.
// A receipt keeps emitted count even when the terminal status is empty.
// Recharge commits the full magazine and clears cadence. Invalid inputs throw
// before mutation. Fixed storage, at most capacity iterations per attempt, no
// allocation. Core F.7 (#rf-smart) exposes values without ownership transfer;
// performance CDSA.32 (transform-static-data-into-the-consumer-layout) prepares
// the interval once. Both design corpora read. See player-combat-resources.md.
#pragma once
#include <cstdint>
namespace bw::game {
struct WeaponResourceSnapshot {
  unsigned ammunition{}, capacity{};
  float cooldown_seconds{};
};
enum class ShotStatus : std::uint8_t {
  accepted,
  cooling_down,
  empty,
  inactive
};
struct WeaponShotReceipt {
  ShotStatus status{ShotStatus::inactive};
  unsigned shot_count{};
  float first_shot_age_seconds{}, interval_seconds{};
};
class WeaponResources {
 public:
  WeaponResources(unsigned capacity, float rate_per_second);
  WeaponResourceSnapshot snapshot() const { return state_; }
  WeaponShotReceipt try_fire();
  void advance(float admitted_seconds);
  void recharge() noexcept;

 private:
  WeaponResourceSnapshot state_;
  float interval_;
};
}  // namespace bw::game
