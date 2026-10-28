// World encounter composition over shared projectile, effect and physics
// services. This stable owner borrows mission definitions, session RNG/audio,
// player simulation and population until teardown. It owns projectile lifetime
// and each admitted ordinary gun/AI controller; actors retain health/reaction
// decisions. Shared AI libraries and immutable snapshots live once per mission.
// Neither weapon nor receiver can advance the world clock or physics provider.
// Admission reserves storage and binds the player's narrow synchronous shot
// receiver last. Input emits before model advancement; world advance updates
// muzzle attachment, moves/query bullets, and routes copied impacts to current
// population identities. Retire weapon handles before bullets, before effects.
// No per-frame allocation; costs follow live projectiles and admitted actors.
// Core I.1 (#ri-explicit) and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md), retrieved in both phases, separate
// capabilities and keep retained capacity explicit. See first-combat-exchange.
#pragma once
#include <array>
#include <cstddef>

#include "bw/spatial/static_collision.hpp"
#include "game/actors/alicia/combat/player_weapon_combat.hpp"
#include "game/combat/damage_definition.hpp"
#include "game/session/audio/game_audio.hpp"
namespace bw::game {
class MissionLoading;
class MissionSimulation;
class WorldPopulation;
class WorldPhysics;
class StagePresentation;
memory::Vector<CharacterAudioProfile> combat_audio_profiles(
    const MissionLoading&, const std::shared_ptr<memory::Domain>&);
class WorldCombat final : private CombatReceivers, public ActorSoundEvents {
 public:
  WorldCombat(const world::WorldClock&, const MissionLoading&,
              MissionSimulation&, WorldPopulation&, WorldPhysics&,
              StagePresentation&, CombatEffects&, SourceRandom&,
              audio::CueOutput&, CharacterAudioLookup, PlayerDamageResources,
              PlayerDefeatResources, const scene::StaticCollision&,
              CharacterVoicePool&, const std::shared_ptr<memory::Domain>&);
  ~WorldCombat() override;
  WorldCombat(const WorldCombat&) = delete;
  WorldCombat& operator=(const WorldCombat&) = delete;
  WorldCombat(WorldCombat&&) = delete;
  WorldCombat& operator=(WorldCombat&&) = delete;
  // Copy current environment before the player's accepted input shot receiver.
  void prepare_player_presentation();
  // World-only pre-actor phase drains prior sound and advances shared AI
  // services.
  void begin_frame(const world::WorldFrame&);
  // Same world teardown that retires placed draws and collision withdraws AI
  // cover observations; callers cannot mutate individual obstacle records.
  void retire_background();
  void advance_actors(const world::WorldFrame&);
  void advance(const world::WorldFrame&);
  void emit(ActorSoundEmission) override;

 private:
  void receive(const CombatImpact&) override;
  MissionSimulation& simulation_;
  WorldPopulation& population_;
  const ActorDamageDefinition& soldier_damage_;
  SourceRandom& random_;
  // Next admitted actor dispatch drains source death notifications. At most
  // fifty active NPCs can die between dispatches; no overflow is dropped.
  // Design I.1/E.4 and MEM.9: narrow recipient and fixed copied queue.
  PlayerDefeatResources player_defeat_;
  static constexpr std::size_t kDefeatCapacity = 50;
  std::array<float, kDefeatCapacity> pending_defeats_{};
  std::size_t pending_defeat_count_{};
  CombatWorld projectiles_;
  PlayerWeaponCombat player_weapon_;
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace bw::game
