// Original health transaction, shared by actor resource owners. This pure
// operation copies observations and returns the next current/recoverable pools;
// it owns no health, clock, actor, physics or presentation. The caller performs
// source eligibility/body scaling first, then commits this result atomically.
// Camera-controlled actors subtract four times the scaled hit from current
// health, while ordinary actors adopt the reduced recoverable ceiling. This
// distinction follows actor+1214, the attached player camera, not its model.
// Invulnerability and already-dead gates preserve timers. Accepted hits reset
// both recovery delays; nonlethal mode restores current=1 and ceiling=maximum.
// Source6879f0; player camera ownership619cc0. Core I.1 (#ri-explicit) makes
// branch inputs and mutation outputs explicit; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps fixed O(1) work
// with no allocation. Both corpora retrieved before design.
#pragma once
#include <cstdint>

namespace bw::game {
enum class HealthDamageMode : std::uint8_t { ordinary, player_camera };
struct HealthDamageInput {
  float current{}, recoverable{}, maximum{}, magnitude{}, scale{};
  HealthDamageMode mode{HealthDamageMode::ordinary};
  bool invulnerable{}, nonlethal{};
};
struct HealthDamageResult {
  float current{}, recoverable{};
  bool accepted{}, alive{};
};
HealthDamageResult apply_health_damage(const HealthDamageInput& input);
}  // namespace bw::game
