// Original successful-shot muzzle presentation. Mission preparation copies the
// selected weapon's visual identities and light descriptor. The stable weapon
// owns its retained flash and one move-only scene light registration; borrowed
// effect emitter and const world clock must outlive it. No gameplay damage,
// random draws, audio or frame publication authority is granted. Accepted shots
// replace prior flash/light, capture current source light RGB, then attach to
// copied muzzle transforms. World frames apply the original linear decay;
// pause preserves lifetime. Retirement stops flash emission and clears light.
// Sprite capacity exhaustion returns no flash as the original allocation can;
// invalid inputs/frames reject. Fixed state, no successful frame allocations.
// Core I.11 (#ri-raw) separates borrowed output from owned registration; MEM.2
// (guidelines/memory/MEM.2-pool-allocator-for-fixed-size-objects.md) keeps
// light storage prepared. Both design corpora read; first-combat-effects.md
// records source producer/lifetime and pinned Unreal scene ownership
// comparison.
#pragma once
#include "bw/content/formats/customization.hpp"
#include "bw/lighting/scene_lighting.hpp"
#include "combat_effects.hpp"
namespace bw::game {
struct WeaponPresentationDefinition {
  std::array<std::int32_t, 2> retained_flash{-1, -1};
  math::Vector3<float> diffuse{}, specular{}, attenuation{};
  float duration{};
  std::int32_t discharge_effect{-1};
  static WeaponPresentationDefinition parse(
      const formats::CustomizationDefinition&, std::string_view original_name);
};
class WeaponPresentation {
 public:
  WeaponPresentation(const world::WorldClock&, WeaponPresentationDefinition,
                     CombatEffectEmitter, scene::LocalLightEmitter&&);
  ~WeaponPresentation();
  WeaponPresentation(const WeaponPresentation&) = delete;
  WeaponPresentation& operator=(const WeaponPresentation&) = delete;
  WeaponPresentation(WeaponPresentation&&) = delete;
  WeaponPresentation& operator=(WeaponPresentation&&) = delete;
  // Per-projectile detached effect uses its distinct authored locator. The
  // effect world owns its natural lifetime; weapon retirement owns only flash.
  // I.4 (#ri-typed) names these independent placements; MEM.9 retains pools.
  void discharge(const math::Affine3<float>& discharge_world);
  void accepted_shot(const math::Affine3<float>& muzzle_world,
                     const math::Vector3<float>& source_light_color,
                     bool source_primary_flash);
  void move(const math::Affine3<float>& muzzle_world);
  void advance(const world::WorldFrame&);
  void retire();

 private:
  const world::WorldClock& clock_;
  WeaponPresentationDefinition definition_;
  CombatEffectEmitter effects_;
  scene::LocalLightEmitter light_;
  std::optional<effects::SpriteHandle> flash_;
  effects::SpritePlacement placement_;
  scene::LocalLight light_value_;
  float intensity_{}, decay_{};
  std::uint64_t sequence_{};
  bool light_active_{};
  void publish_light();
};
}  // namespace bw::game
