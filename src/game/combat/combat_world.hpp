// World-owned persistent projectile response and presentation. Actor weapon
// owners decide when/where to shoot and own firing/reload presentation. This
// owner retains bullets and their attached sprite handles, routes copied hits
// to a synchronous game receiver, and emits original surface/near-pass cues.
// Physics owns intersections; receivers own eligibility, health and reactions.
// Definitions/sound profiles, issuing clock, effects and audio outlive this
// owner. Receiver is borrowed only during advance and must not reenter. Actor
// identity is copied source-generation identity, never a native body pointer.
// Emit prepares the original direction transform; advance consumes one world
// frame, moves attached effects, retires bullet presentation on impact/range,
// dispatches source surface feedback, then delivers the copied actor hit.
// Destruction retires attached effects while their world still exists. The
// session owns independent one-shot audio through natural completion/teardown.
// Fixed capacity allocated at admission; O(active bullets), no frame
// allocation. Source61aac0/61ba90/61b8e0/61bbf0. Core I.1 (#ri-explicit)
// separates query, mutation and response authority; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) fixes retained
// storage. Both corpora retrieved during design; first-combat-exchange.md
// records scope.
#pragma once
#include "combat_effects.hpp"
#include "projectile_world.hpp"
#include "weapon_audio.hpp"

namespace bw::game {
struct CombatImpact {
  std::uint64_t target{}, source{};
  std::uint8_t body{}, surface{};
  float damage{};
  math::Vector3<float> position{}, normal{}, direction{};
  physics::ReceiverId receiver;
};
class CombatReceivers {
 public:
  CombatReceivers() = default;
  virtual ~CombatReceivers() = default;
  CombatReceivers(const CombatReceivers&) = delete;
  CombatReceivers& operator=(const CombatReceivers&) = delete;
  CombatReceivers(CombatReceivers&&) = delete;
  CombatReceivers& operator=(CombatReceivers&&) = delete;
  virtual void receive(const CombatImpact&) = 0;
};
class CombatProjectileEmitter;
class CombatWorld {
 public:
  CombatWorld(const world::WorldClock&, physics::Queries, ProjectileLimits,
              CombatEffects&, audio::CueOutput&,
              const std::shared_ptr<memory::Domain>&);
  ~CombatWorld();
  CombatWorld(const CombatWorld&) = delete;
  CombatWorld& operator=(const CombatWorld&) = delete;
  CombatWorld(CombatWorld&&) = delete;
  CombatWorld& operator=(CombatWorld&&) = delete;
  ProjectileId emit(const StraightShot&, const WeaponSoundProfile&);
  CombatProjectileEmitter emitter() noexcept;
  void advance(const world::WorldFrame&, const math::Vector3<float>& camera,
               CombatReceivers&);
  void retire();
  std::size_t active_count() const { return projectiles_.active_count(); }

 private:
  struct Presentation {
    std::uint32_t generation{};
    std::optional<effects::SpriteHandle> sprite;
    const WeaponSoundProfile* sound{};
  };
  ProjectileWorld projectiles_;
  CombatEffects& effects_;
  audio::CueOutput& audio_;
  memory::Vector<Presentation> presentation_;
  void stop(Presentation&);
  void cue(std::uint16_t, const math::Vector3<float>&);
};
// Non-owning synchronous admission only. A weapon can create a shot but cannot
// advance/retire the world or access another projectile. The world outlives
// all weapons; copied capabilities do not extend its lifetime. I.11/MEM.9.
class CombatProjectileEmitter {
 public:
  ProjectileId emit(const StraightShot&, const WeaponSoundProfile&) const;

 private:
  friend class CombatWorld;
  explicit CombatProjectileEmitter(CombatWorld& world) : world_(world) {}
  std::reference_wrapper<CombatWorld> world_;
};
}  // namespace bw::game
