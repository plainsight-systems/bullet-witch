// Native cockpit game adapter. Translate player resource receipts, targeting
// and ring observations into the HUD's source lanes; never acquire mutation
// handles or infer recharge from ammo deltas. Explicit activation distinguishes
// the current inspection host from the unimplemented Lua visibility binding.
// World admission suppresses updates: zero elapsed alone cannot freeze
// immediate task phases. Drawing can reuse the retained cockpit on a consumed
// resume frame; hidden activation explicitly clears the retained sprite
// submission. Output and RNG outlive this stable owner; prepared resource
// lifetimes remain with the surface. Frame work copies bounded values and emits
// existing commands without allocation. Design Core I.1 (#ri-explicit), C.9
// (#rc-private) keeps the launch layer out of game HUD policy; performance
// MEM.9 (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) retains fixed
// state.
#pragma once
#include "game/actors/alicia/player_resources.hpp"
#include "gameplay_hud.hpp"

namespace bw::game {
enum class CockpitActivation : std::uint8_t { hidden, inspection };
class PlayerCockpit {
 public:
  PlayerCockpit(GameplayHudOutput& surface, SourceRandom& random,
                CockpitActivation activation);
  PlayerCockpit(const PlayerCockpit&) = delete;
  PlayerCockpit& operator=(const PlayerCockpit&) = delete;
  PlayerCockpit(PlayerCockpit&&) = delete;
  PlayerCockpit& operator=(PlayerCockpit&&) = delete;
  ~PlayerCockpit() = default;
  void advance(const world::WorldFrame& frame,
               const PlayerResourceSnapshot& resources, bool aiming,
               const SpellRingPresentation& ring);
  void show(std::string_view diagnostic = {});
  const GameplayHud& presentation() const { return hud_; }

 private:
  GameplayHudOutput& surface_;
  SourceRandom& random_;
  CockpitActivation activation_;
  GameplayHud hud_;
};
}  // namespace bw::game
