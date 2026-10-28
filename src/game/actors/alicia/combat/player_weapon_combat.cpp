// Admit source resource receipts at the input phase, using the existing model
// pose and previous completed camera. Advance attached presentation after the
// model phase. Core I.1/MEM.9 reread for implementation; copied observations
// and shared query/emission services keep physics and resource owners external.
#include "player_weapon_combat.hpp"

#include <stdexcept>

#include "game/actors/animation/combat_pose.hpp"
namespace bw::game {
PlayerWeaponCombat::PlayerWeaponCombat(
    const world::WorldClock& clock, const StraightWeaponDefinition& definition,
    const WeaponSoundProfile& sounds, WeaponPresentationDefinition presentation,
    SourceRandom& random, CombatProjectileEmitter combat,
    CombatEffectEmitter effects, scene::LocalLightEmitter&& light,
    audio::CueOutput& audio, physics::Queries queries, std::uint64_t actor,
    ShotQueryBanks banks, math::Vector3<float> source_light_color)
    : definition_(definition),
      queries_(std::move(queries)),
      banks_(banks),
      light_color_(source_light_color),
      presentation_(clock, presentation, effects, std::move(light)),
      emitter_(clock, definition, sounds, random, combat, presentation_, audio,
               actor, banks.scenery, banks.actors) {}
void PlayerWeaponCombat::attempt(const world::WorldFrame& frame,
                                 const ShotResourceResult& receipt,
                                 const PlayerShotObservation& observation) {
  if (observation.weapon.weapon_class != receipt.weapon) {
    throw std::invalid_argument("Weapon receipt and observed model differ");
  }
  if (receipt.weapon != 0) return;
  const auto origin = original_weapon_attachment_pose(
      &observation.weapon.attachments.get(), observation.weapon.skin,
      observation.weapon.model,
      definition_.effects.projectile_origin_attachment);
  const auto discharge = original_weapon_attachment_pose(
      &observation.weapon.attachments.get(), observation.weapon.skin,
      observation.weapon.model, definition_.effects.discharge_attachment);
  const auto target =
      player_shot_target(queries_,
                         {observation.camera.eye, observation.camera.forward,
                          observation.actor_position, definition_.range},
                         banks_);
  const auto geometry = player_shot_geometry(
      {origin[3], target.position, observation.actor_position, origin[2],
       observation.actor_radius, target.hit.has_value()});
  emitter_.attempt(frame,
                   {receipt.shot_count, receipt.level,
                    receipt.first_shot_age_seconds, receipt.interval_seconds},
                   {.shot = geometry,
                    .muzzle_world = origin,
                    .discharge_world = discharge,
                    .light_color = light_color_});
}
void PlayerWeaponCombat::recharge(const math::Vector3<float>& position,
                                  unsigned weapon_class) {
  if (weapon_class == 0) emitter_.recharge(position);
}
void PlayerWeaponCombat::advance(const world::WorldFrame& frame,
                                 const PlayerWeaponPose& pose) {
  const bool selected = pose.weapon_class == 0;
  if (!selected) {
    if (selected_) emitter_.retire();
    selected_ = false;
    return;
  }
  selected_ = true;
  emitter_.advance(frame, weapon_spread_widens(pose.body_slot),
                   original_weapon_attachment_pose(
                       &pose.attachments.get(), pose.skin, pose.model,
                       definition_.effects.projectile_origin_attachment));
}
}  // namespace bw::game
