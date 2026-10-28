// Original ordinary straight-projectile settings, shared by player/enemy gun
// owners. Copies verified scalar/identity fields from customization once; no
// pointer into source bytes survives. Does not own magazines, world time,
// audio, effect instances or collision. Unsupported dispatch/ray count rejects
// before admission; this does not claim elemental, shotgun or missile behavior.
// Damage interpolation is the original 61b1d0 range curve, before
// actor-specific body/armor/reaction policy. Source level indexes clamp below1
// to first lane; values above3 reject instead of reading past original tables.
// Surface effects retain original integer identities, including negative
// absence. Fixed copied arrays; O(surface count) prepare, O(1) damage, no
// update allocation/locking. Core I.1 (#ri-explicit) separates damage from
// geometry; performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// prepares consumer fields once. Both corpora queried/read before design.
#pragma once
#include "bw/content/formats/customization.hpp"
namespace bw::game {
struct ProjectileDamage {
  std::array<float, 3> base{}, far_multiplier{};
  float falloff_start{}, falloff_end{};
  float at_distance(unsigned level, float traveled_distance) const;
};
struct WeaponEffects {
  static constexpr std::size_t kSurfaceCount = 40;
  std::int32_t retained_flash{-1}, projectile{-1}, muzzle{-1};
  std::int32_t discharge_attachment{-1};
  std::int32_t projectile_origin_attachment{-1};
  std::array<std::int32_t, kSurfaceCount> surface_impact{};
};
struct WeaponSpread {
  float minimum_radians{}, maximum_radians{};
  float widen_seconds{}, recover_seconds{};
};
struct StraightWeaponDefinition {
  ProjectileDamage damage;
  WeaponEffects effects;
  float range{}, speed{}, spread_degrees{}, rate_per_second{};
  unsigned magazine_capacity{};
  std::uint32_t near_pass_cue{};
  WeaponSpread spread;
  static StraightWeaponDefinition parse(const formats::CustomizationDefinition&,
                                        std::string_view original_name);
};
}  // namespace bw::game
