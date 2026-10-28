// Source NPC Runner destination/rate/posture selection, separate from player
// hardware input. Inputs are copied actor observations; output is a bounded
// animation request plus root-motion direction. No world clock, resources,
// physics, navigation or actor state are owned. The caller retains heading
// between admitted world updates and applies authored root displacement.
// Positive turn limits rotate the retained heading toward the destination by
// at most limit*world_delta. Source rate and family remain independent; no
// analog-stick deadzone or walk/dash threshold is introduced. Invalid inputs
// reject before producing a result. Core I.1 (#ri-explicit), read for design,
// exposes those authorities; performance MEM.9 keeps fixed allocation-free
// work. Original contracts and reference comparison: first-combat-ai.md.
#pragma once
#include "bw/math/vector.hpp"
#include "game/actors/animation/action_motion_plan.hpp"

namespace bw::game {
struct NpcLocomotionInput {
  math::Vector3<float> position{}, destination{}, retained_heading{0, 0, 1},
      anchor_forward{0, 0, 1};
  std::uint32_t posture{};
  float playback_rate{1}, turn_limit_radians{}, signed_turn_rate{},
      world_delta_seconds{};
  bool stopped{}, weapon_raised{};
};
struct NpcLocomotionSelection {
  ActionMotionRequest motion;
  math::Vector3<float> retained_heading{}, movement_direction{};
  std::optional<math::Vector3<float>> facing_request;
};
NpcLocomotionSelection select_npc_locomotion(const NpcLocomotionInput&);
}  // namespace bw::game
