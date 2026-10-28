// One admitted NPC's source behavior/action, navigation and weapon
// owners. Audio is borrowed from the separate per-actor owner. WorldCombat
// owns this controller and destroys it before its borrowed
// actor. Mission/session services and immutable definitions outlive it.
// Construction prepares bounded storage; explicit start follows complete world
// admission. The world calls advance before actor motion, then after_pose
// exactly once after the completed pose. Neither phase steps the world, physics
// or renderer. Commands change only the borrowed actor through named
// operations; projectile submission uses an emission-only capability.
// Observations combine copied scene facts with real shared collision queries.
// Missing source/action data fails explicitly; no successful placeholder
// actions or substitute targets. Pause performs neither phase. stop retires
// action/audio/effect ownership before actor/resource teardown. Storage scales
// with prepared navigation and the fixed population limit; frames allocate no
// dynamic storage. Core I.1
// (#ri-explicit) exposes authority and ordering; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) prepares owners once.
// Both design corpora read; first-combat-ai.md records native contracts.
#pragma once
#include <functional>

#include "bw/spatial/static_collision.hpp"
#include "game/actors/audio/npc_audio.hpp"
#include "game/actors/sound_stimuli.hpp"
#include "game/combat/melee_events.hpp"
#include "game/combat/weapon_emitter.hpp"
#include "game/combat/weapon_presentation.hpp"
#include "navigation_projection.hpp"
#include "npc_actor.hpp"
#include "npc_brain.hpp"
#include "npc_combat_metadata.hpp"
#include "npc_encounter_scene.hpp"
#include "target_requests.hpp"
namespace bw::game {
struct NpcCombatServices {
  std::reference_wrapper<const world::WorldClock> clock;
  std::reference_wrapper<BehaviorSchedule> behaviors;
  std::reference_wrapper<PerceptionSchedule> perceptions;
  std::reference_wrapper<NpcMovementSchedule> movement_schedule;
  std::reference_wrapper<SourceRandom> random;
  std::reference_wrapper<NpcBehaviorLibrary> scripts;
  std::reference_wrapper<const NativeActionLibrary> actions;
  std::reference_wrapper<const NpcAiSettings> settings;
  std::reference_wrapper<const NpcEncounterScene> scene;
  std::reference_wrapper<NpcEncounterCommands> commands;
  std::reference_wrapper<const scene::StaticCollision> collision;
  physics::Queries queries;
  std::reference_wrapper<const formats::NavigationArchive> navigation;
  std::size_t navigation_group{};
  std::reference_wrapper<audio::CueOutput> audio;
  CombatProjectileEmitter projectiles;
  std::reference_wrapper<MeleeEmission> melee;
  CombatEffectEmitter effects;
  std::uint16_t scenery_layers{}, actor_layers{};
};
struct NpcCombatResources {
  std::reference_wrapper<const StraightWeaponDefinition> weapon;
  std::reference_wrapper<const WeaponSoundProfile> weapon_sounds;
  std::reference_wrapper<NpcAudio> character_audio;
  WeaponPresentationDefinition weapon_presentation;
  std::string_view initial_behavior, path, secondary_path;
  float body_radius{}, body_height{}, dodge_threshold{};
  bool project_approach{};
};
class NpcCombatController {
 public:
  NpcCombatController(NpcActor&, NpcCombatMetadata, NpcCombatServices,
                      NpcCombatResources, scene::LocalLightEmitter&&,
                      const std::shared_ptr<memory::Domain>&);
  NpcCombatController(const NpcCombatController&) = delete;
  NpcCombatController& operator=(const NpcCombatController&) = delete;
  NpcCombatController(NpcCombatController&&) = delete;
  NpcCombatController& operator=(NpcCombatController&&) = delete;
  ~NpcCombatController();
  void start();
  // World service phase only: process previously queued collision route jobs
  // before any actor consumes their copied results in this frame.
  void advance_navigation_jobs(const world::WorldFrame&);
  void advance(const world::WorldFrame&);
  void after_pose(const world::WorldFrame&);
  void stop();
  // World-filtered message is queued, never synchronously reenters the brain.
  void receive_target(NpcTargetRequest);
  void receive_sound(const ActorSoundStimulus&);
  // Original camera-owned StateDie entry broadcasts message eight; no sound
  // extension is present. Recipient/radius filtering belongs to the world.
  void receive_player_death(const math::Vector3<float>&);

 private:
  void receive_attention(std::size_t entry, std::uint32_t message,
                         const math::Vector3<float>&,
                         std::optional<std::uint16_t> sound);
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace bw::game
