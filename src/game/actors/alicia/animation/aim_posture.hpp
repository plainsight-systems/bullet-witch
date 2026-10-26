// Source Runner/Shooter target and retained posture state. Own fixed scalar
// state; caller owns model/anchor matrices, authored pitch limits and named
// chain binding. Target preparation requires unit finite directions and finite
// nonsingular matrices; facing retains authored scale as specified below.
// Malformed requests throw before state mutation. Targets are model
// component directions consumed by AimChainStep before hierarchy. Clock input
// is simulation seconds; paused callers do not advance. No allocations.
// Core E.4 (#re-design-invariants) and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md), queried/read for design and
// implementation, require explicit validation and fixed steady-state storage.
// Original source and pinned Unreal comparison: raised-aim-posture.md.
#pragma once
#include "bw/math/matrix.hpp"
namespace bw::game {
struct AimPostureTarget {
  math::Matrix4<float> anchor{}, model{};
  math::Vector3<float> retained_direction{0, 0, 1};
  std::array<float, 4> pitch_degrees{};
  bool crouched{}, camera_present{true};
};
math::Vector3<float> aim_posture_target(const AimPostureTarget& input);
class AimPostureBlend {
 public:
  // Repeated calls while disabled fade using the retained last target. A
  // zero-time appearance preview preserves all clocks and weights.
  float advance(float seconds, bool enabled);
  float weight() const noexcept { return weight_; }

 private:
  float weight_{};
};
}  // namespace bw::game
