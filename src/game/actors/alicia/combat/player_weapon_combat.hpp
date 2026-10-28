// Player-owned ordinary gun emission over the shared world combat service.
// The input/action owner invokes attempts synchronously before advancing the
// character model, preserving the source muzzle/camera observation phase.
// Retained flash/light attachment then follows completed weapon pose once per
// admitted world frame. Profiles, session RNG, output and combat world outlive
// this owner; borrowed pose spans exist only during calls. It owns one weapon's
// audiovisual handles, never health, ammo, progression or the physics world.
// Source resource receipts supply count and timing debt. Nonordinary weapon
// classes are outside this admitted slice and retire this gun's presentation.
// Query banks and identity come from world composition; no implicit hostility.
// Validate prepared bone references at use, then scene/actor target query,
// source muzzle correction, persistent emission and original presentation.
// No frame allocation; at most two spatial queries per firing attempt.
// Core I.1 (#ri-explicit) separates synchronous action and world advance;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) fixes owner
// storage. Both corpora read in design. See first-combat-exchange.md.
#pragma once
#include "bw/animation/pose.hpp"
#include "bw/content/formats/control_points.hpp"
#include "bw/render/scene/view.hpp"
#include "game/actors/alicia/player_resources.hpp"
#include "game/combat/shot_target.hpp"
#include "game/combat/weapon_emitter.hpp"
#include "game/combat/weapon_presentation.hpp"
namespace bw::game {
struct PlayerWeaponPose {
  math::Affine3<float> model;
  std::span<const animation::Matrix> skin;
  std::reference_wrapper<const formats::ControlPointDefinition> attachments;
  unsigned weapon_class{};
  std::size_t body_slot{};
};
struct PlayerShotObservation {
  PlayerWeaponPose weapon;
  render::ViewPose camera;
  math::Vector3<float> actor_position{};
  float actor_radius{};
};
class PlayerWeaponReceiver {
 public:
  virtual ~PlayerWeaponReceiver() = default;
  virtual void attempt(const world::WorldFrame&, const ShotResourceResult&,
                       const PlayerShotObservation&) = 0;
  virtual void recharge(const math::Vector3<float>&, unsigned weapon_class) = 0;

 protected:
  PlayerWeaponReceiver() = default;
  PlayerWeaponReceiver(const PlayerWeaponReceiver&) = delete;
  PlayerWeaponReceiver& operator=(const PlayerWeaponReceiver&) = delete;
  PlayerWeaponReceiver(PlayerWeaponReceiver&&) = delete;
  PlayerWeaponReceiver& operator=(PlayerWeaponReceiver&&) = delete;
};
class PlayerWeaponCombat final : public PlayerWeaponReceiver {
 public:
  PlayerWeaponCombat(const world::WorldClock&, const StraightWeaponDefinition&,
                     const WeaponSoundProfile&, WeaponPresentationDefinition,
                     SourceRandom&, CombatProjectileEmitter,
                     CombatEffectEmitter, scene::LocalLightEmitter&&,
                     audio::CueOutput&, physics::Queries, std::uint64_t actor,
                     ShotQueryBanks, math::Vector3<float> source_light_color);
  void attempt(const world::WorldFrame&, const ShotResourceResult&,
               const PlayerShotObservation&) override;
  void recharge(const math::Vector3<float>&, unsigned weapon_class) override;
  // World publishes the current completed environment before input emission;
  // each original muzzle light samples that value at creation.
  void set_environment_light(math::Vector3<float> color) {
    light_color_ = color;
  }
  void advance(const world::WorldFrame&, const PlayerWeaponPose&);

 private:
  const StraightWeaponDefinition& definition_;
  physics::Queries queries_;
  ShotQueryBanks banks_;
  math::Vector3<float> light_color_;
  WeaponPresentation presentation_;
  WeaponEmitter emitter_;
  bool selected_{true};
};
}  // namespace bw::game
