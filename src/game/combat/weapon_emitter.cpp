// Emit the original per-call burst after resource admission. The unconditional
// first RNG draw belongs to the common gun/barrel path even when cooldown
// rejects a shot; each emitted direction consumes two subsequent cone draws.
// Audio and muzzle presentation are requested once after the nonempty burst.
// Core I.11/CDSA.32 were reread for implementation; all owners remain external
// and source policy is consumed without per-frame decoding/allocation.
// C.36 (#rc-dtor-fail) and MEM.9 were reread for teardown diagnostics: report
// cleanup failure without throwing or adding successful frame allocations.
#include "weapon_emitter.hpp"

#include <cmath>
#include <cstdio>
#include <stdexcept>

#include "game/session/compatibility/source_random.hpp"
#include "weapon_presentation.hpp"
namespace bw::game {
WeaponEmitter::WeaponEmitter(
    const world::WorldClock& clock, const StraightWeaponDefinition& definition,
    const WeaponSoundProfile& sounds, SourceRandom& random,
    CombatProjectileEmitter combat, WeaponPresentation& presentation,
    audio::CueOutput& audio, std::uint64_t actor, std::uint16_t scenery_layers,
    std::uint16_t actor_layers)
    : clock_(clock),
      definition_(definition),
      sounds_(sounds),
      random_(random),
      combat_(combat),
      presentation_(presentation),
      audio_(sounds, audio),
      actor_(actor),
      scenery_layers_(scenery_layers),
      actor_layers_(actor_layers),
      spread_(definition.spread.minimum_radians) {
  if (actor == 0 || scenery_layers == 0 || actor_layers == 0) {
    throw std::invalid_argument("Invalid weapon emission identity or banks");
  }
}
WeaponEmitter::~WeaponEmitter() {
  try {
    retire();
  } catch (...) {
    std::fputs(
        "Weapon emitter retirement failed; session cleanup remains required\n",
        stderr);
  }
}
void WeaponEmitter::attempt(const world::WorldFrame& frame,
                            WeaponEmission emission,
                            const WeaponEmissionGeometry& geometry) {
  if (!frame.belongs_to(clock_) || !frame.admission().simulation ||
      frame.sequence() <= sequence_ || frame.sequence() < attempt_sequence_ ||
      emission.level > 3 || !std::isfinite(emission.first_age_seconds) ||
      !std::isfinite(emission.interval_seconds) ||
      emission.interval_seconds <= 0 ||
      emission.count > definition_.magazine_capacity ||
      (emission.count > 0 && emission.first_age_seconds < 0)) {
    throw std::invalid_argument("Invalid source weapon emission schedule");
  }
  attempt_sequence_ = frame.sequence();
  // Selected WA0/WG1 have no rotating-barrel receiver, but native616530 still
  // consumes its common angular-speed random sample before checking cooldown.
  (void)random_.unit();
  for (unsigned index = 0; index < emission.count; ++index) {
    const auto direction =
        sample_shot_direction(geometry.shot, spread_, random_);
    (void)combat_.emit(
        {&definition_, geometry.shot.origin, direction,
         geometry.shot.scenery_reference,
         emission.first_age_seconds -
             static_cast<float>(index) * emission.interval_seconds,
         actor_, emission.level, scenery_layers_, actor_layers_},
        sounds_);
    presentation_.discharge(geometry.discharge_world);
  }
  if (emission.count == 0) return;
  presentation_.accepted_shot(geometry.muzzle_world, geometry.light_color,
                              true);
  audio_.shot(math::translation(geometry.muzzle_world));
}
void WeaponEmitter::advance(const world::WorldFrame& frame, bool widening,
                            const math::Affine3<float>& muzzle_world) {
  if (!frame.belongs_to(clock_) || frame.sequence() <= sequence_) {
    throw std::invalid_argument("Invalid weapon emission frame");
  }
  sequence_ = frame.sequence();
  if (!frame.admission().simulation) return;
  spread_ = advance_weapon_spread(definition_.spread, spread_,
                                  frame.delta_seconds(), widening);
  presentation_.move(muzzle_world);
  presentation_.advance(frame);
  audio_.advance(frame.delta_seconds(), math::translation(muzzle_world));
}
void WeaponEmitter::recharge(const math::Vector3<float>& position) {
  audio_.recharge(position);
}
void WeaponEmitter::retire() {
  presentation_.retire();
  audio_.retire();
}
}  // namespace bw::game
