// Original character root-displacement reference shared by player and NPC.
// Consume a sampled authored local transform; return the source rebuilt basis.
// This is compatibility policy above generic math, not a second math library.
// No ownership, clock, pose mutation or allocation. Invalid degenerate authored
// basis throws. Core I.1 (#ri-explicit) names the input, performance MEM.9
// keeps fixed work; both design corpora read. Source664be0/409f20 and its
// established player implementation are recorded in
// player-input-locomotion-policy.md.
#pragma once
#include "bw/animation/pose.hpp"
namespace bw::game {
animation::Matrix original_root_motion_basis(const formats::SkeletonTransform&);
}  // namespace bw::game
