// Per-weapon original straight-shot emission. Resource/Shooter owners admit a
// copied schedule; this owner consumes source RNG in call order, emits
// persistent projectiles, and requests retained audiovisual presentation once
// per nonempty burst. It owns neither ammunition, targeting, physics nor the
// world clock. Definitions/profile, session RNG, combat world, presentation and
// audio output outlive it. World supplies the issuing frame and advances it
// once after actor requests; multiple source calls before that update retain
// their RNG order. Resource ownership, not this renderer, admits repeated
// emission receipts. Pause does not consume RNG or age sounds/spread. No frame
// allocation. Validate receipts before mutation; capacities/backend failures
// propagate. Source616530/615fc0/617a30; Core I.11 (#ri-raw) keeps dependencies
// borrowed, performance CDSA.32
// (transform-static-data-into-the-consumer-layout) consumes prepared immutable
// profiles. Both corpora read in design and implementation.
#pragma once
#include "combat_world.hpp"
#include "shot_geometry.hpp"

namespace bw::game {
class WeaponPresentation;
struct WeaponEmission {
  unsigned count{}, level{};
  float first_age_seconds{}, interval_seconds{};
};
struct WeaponEmissionGeometry {
  ShotGeometry shot;
  // The retained barrel flash/audio and detached discharge are distinct
  // authored attachment frames (Core I.4); neither aliases raw skeleton slots.
  math::Affine3<float> muzzle_world{}, discharge_world{};
  math::Vector3<float> light_color{};
};
class WeaponEmitter {
 public:
  WeaponEmitter(const world::WorldClock&, const StraightWeaponDefinition&,
                const WeaponSoundProfile&, SourceRandom&,
                CombatProjectileEmitter, WeaponPresentation&, audio::CueOutput&,
                std::uint64_t actor, std::uint16_t scenery_layers,
                std::uint16_t actor_layers);
  WeaponEmitter(const WeaponEmitter&) = delete;
  WeaponEmitter& operator=(const WeaponEmitter&) = delete;
  WeaponEmitter(WeaponEmitter&&) = delete;
  WeaponEmitter& operator=(WeaponEmitter&&) = delete;
  ~WeaponEmitter();
  void attempt(const world::WorldFrame&, WeaponEmission,
               const WeaponEmissionGeometry&);
  void advance(const world::WorldFrame&, bool widening,
               const math::Affine3<float>& muzzle_world);
  void recharge(const math::Vector3<float>& position);
  void retire();

 private:
  const world::WorldClock& clock_;
  const StraightWeaponDefinition& definition_;
  const WeaponSoundProfile& sounds_;
  SourceRandom& random_;
  CombatProjectileEmitter combat_;
  WeaponPresentation& presentation_;
  WeaponAudio audio_;
  std::uint64_t actor_{}, sequence_{}, attempt_sequence_{};
  std::uint16_t scenery_layers_{}, actor_layers_{};
  float spread_{};
};
}  // namespace bw::game
