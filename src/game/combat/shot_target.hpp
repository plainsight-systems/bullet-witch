// Original camera targeting against the world's scenery and actor query banks.
// This synchronous adapter borrows only immutable query authority. It
// constructs the source camera segment, clips scenery with its one-centimetre
// clearance, then clips eligible actors against that shortened segment. The
// copied result distinguishes a hit from the far endpoint; weapon placement
// owns subsequent muzzle correction. No damage, ammo, clock or actor mutation
// occurs here. Caller supplies source side/bank eligibility.
// Nonfinite/degenerate view throws before querying; provider failures
// propagate. At most two spatial queries, fixed scratch and no retained
// references or allocations. Core I.1
// (#ri-explicit) names query policy; performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps fixed work storage. Both corpora
// retrieved for design. Source62ed80→5f3a40→5f5cb0; see
// first-combat-exchange.md.
#pragma once
#include "bw/physics/world.hpp"
#include "shot_geometry.hpp"
namespace bw::game {
// Shared camera/projectile eligibility: disabled receivers never participate;
// actor states additionally suppress ordinary shot hits through their bit two.
inline constexpr std::uint32_t kShotDisabledReceiverMask = 0x80000000U;
inline constexpr std::uint32_t kShotActorExclusionMask =
    kShotDisabledReceiverMask | 2U;
struct ShotQueryBanks {
  std::uint16_t scenery{}, actors{};
};
struct ShotTarget {
  math::Vector3<float> position{};
  std::optional<physics::Hit> hit;
};
ShotTarget player_shot_target(const physics::Queries&, const PlayerShotView&,
                              ShotQueryBanks);
}  // namespace bw::game
