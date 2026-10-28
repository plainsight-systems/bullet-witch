// Source projectile lifetime owns attached effects; original impact collection
// publishes surface feedback before delivering actor damage. I.1 and MEM.9
// were retrieved again for these bounded copied responses and retained handles.
#include "combat_world.hpp"

#include <cstdio>
#include <stdexcept>

#include "shot_geometry.hpp"

namespace bw::game {
CombatProjectileEmitter CombatWorld::emitter() noexcept {
  return CombatProjectileEmitter(*this);
}
ProjectileId CombatProjectileEmitter::emit(
    const StraightShot& shot, const WeaponSoundProfile& sound) const {
  return world_.get().emit(shot, sound);
}
CombatWorld::CombatWorld(const world::WorldClock& clock,
                         physics::Queries queries, ProjectileLimits limits,
                         CombatEffects& effects, audio::CueOutput& audio,
                         const std::shared_ptr<memory::Domain>& domain)
    : projectiles_(clock, std::move(queries), limits, domain),
      effects_(effects),
      audio_(audio),
      presentation_(limits.capacity, Presentation{},
                    memory::Allocator<Presentation>(domain)) {}
CombatWorld::~CombatWorld() {
  try {
    retire();
  } catch (...) {
    std::fputs(
        "Combat presentation retirement failed; world cleanup required\n",
        stderr);
  }
}
void CombatWorld::stop(Presentation& presentation) {
  if (presentation.sprite && effects_.alive(*presentation.sprite) &&
      !effects_.stop(*presentation.sprite)) {
    throw std::runtime_error("Projectile effect retirement failed");
  }
  presentation.sprite.reset();
}
void CombatWorld::retire() {
  for (auto& presentation : presentation_) stop(presentation);
  projectiles_.retire();
}
ProjectileId CombatWorld::emit(const StraightShot& shot,
                               const WeaponSoundProfile& sound) {
  const auto transform =
      math::affine_part(shot_forward_transform(shot.muzzle, shot.direction));
  const auto id = projectiles_.emit(shot);
  auto& presentation = presentation_.at(id.slot);
  stop(presentation);
  presentation.generation = id.generation;
  presentation.sound = &sound;
  if (shot.definition->effects.projectile >= 0) {
    presentation.sprite = effects_.spawn(
        static_cast<std::uint32_t>(shot.definition->effects.projectile),
        {.transform = transform});
    if (!presentation.sprite) {
      throw std::length_error("Projectile effect admission exhausted");
    }
  }
  return id;
}
void CombatWorld::cue(std::uint16_t index,
                      const math::Vector3<float>& position) {
  if (!audio_.play({.bank = 0, .cue = index, .position = position})) {
    throw std::runtime_error("Projectile sound admission failed");
  }
}
void CombatWorld::advance(const world::WorldFrame& frame,
                          const math::Vector3<float>& camera,
                          CombatReceivers& receivers) {
  projectiles_.advance(frame, camera);
  if (!frame.admission().simulation) return;
  for (const auto& event : projectiles_.events()) {
    auto& presentation = presentation_.at(event.projectile.slot);
    if (presentation.generation != event.projectile.generation ||
        !presentation.sound) {
      throw std::logic_error("Projectile presentation identity mismatch");
    }
    if (event.kind == ProjectileEventKind::moved) {
      if (presentation.sprite && effects_.alive(*presentation.sprite)) {
        if (!effects_.move(
                *presentation.sprite,
                {.transform = math::affine_part(shot_forward_transform(
                     event.position, event.direction))})) {
          throw std::runtime_error("Projectile effect movement failed");
        }
      }
      if (event.near_pass) {
        cue(static_cast<std::uint16_t>(event.definition->near_pass_cue),
            *event.near_pass);
      }
      continue;
    }
    stop(presentation);
    if (event.kind != ProjectileEventKind::impact) continue;
    if (!event.hit) throw std::logic_error("Projectile impact lacks contact");
    const auto& hit = *event.hit;
    const bool actor = hit.user_data != 0;
    constexpr std::uint32_t kBodyMask = 0xff;
    constexpr unsigned kSurfaceShift = 8;
    const auto surface = actor ? hit.surface >> kSurfaceShift : hit.surface;
    const auto body = actor ? hit.surface & kBodyMask : 0;
    if (surface >= WeaponEffects::kSurfaceCount) {
      throw std::out_of_range("Projectile contact has unsupported surface");
    }
    const auto effect = event.definition->effects.surface_impact.at(surface);
    if (effect >= 0 &&
        !effects_.spawn(static_cast<std::uint32_t>(effect),
                        {.transform = math::affine_part(shot_forward_transform(
                             event.position, hit.normal))})) {
      throw std::length_error("Impact effect admission exhausted");
    }
    cue(presentation.sound->impact.at(surface), event.position);
    if (actor) {
      receivers.receive(
          {hit.user_data, event.owner, static_cast<std::uint8_t>(body),
           static_cast<std::uint8_t>(surface),
           event.definition->damage.at_distance(event.level,
                                                event.damage_distance),
           event.position, hit.normal, event.direction, hit.receiver});
    }
  }
}
}  // namespace bw::game
