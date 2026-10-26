// Alicia's support-hand binding for the authorized PC animation improvement.
// The original registers LeftArm/LeftForeArm/LeftHand and weapon control points
// 100/101, but its PC solver is empty. This adapter supplies those authored
// targets to the shared two-bone solver; it does not select actions or time.
// Prepare owned control-point metadata and fixed scratch per appearance.
// Evaluate reads completed weapon skin matrices and a clean body world pose,
// returns three owned world overrides, and retains no previous result. Caller
// combines these with other modifiers before one atomic pose publication.
// The caller supplies the existing raised-posture weight. No clock, mutable
// resource borrow, per-weapon offset, frame allocation or archive read.
// O(control points * influences) per publication. Both phases Core I.1
// (#ri-explicit) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) make coordinate
// spaces/ownership and preparation costs explicit.
#pragma once
#include <functional>

#include "bw/animation/secondary_motion.hpp"
#include "bw/content/formats/model.hpp"

namespace bw::game {
struct SupportArmPoses {
  std::reference_wrapper<const animation::SkeletonPose> body;
  std::reference_wrapper<const animation::SkeletonPose> weapon;
};
struct SupportArmSource {
  std::reference_wrapper<const formats::ModelDefinition> body;
  std::reference_wrapper<const formats::ModelDefinition> weapon;
  std::span<const std::uint8_t> weapon_control_points;
};
class AliciaSupportArm {
 public:
  AliciaSupportArm(SupportArmSource source,
                   const std::shared_ptr<memory::Domain>& domain);
  // Input poses share world space. Zero weight leaves the body untouched;
  // unreachable targets clamp without stretching. Invalid setup/weight rejects
  // before mutation. Hand orientation stays authored; roll/finger descendants
  // follow the pose owner's existing atomic world-override composition.
  std::optional<std::array<animation::BoneWorldOverride, 3>> evaluate(
      SupportArmPoses poses, float raised_weight);

 private:
  formats::ControlPointDefinition control_points_;
  memory::Vector<animation::Matrix> frames_;
  std::array<std::size_t, 3> bones_{};
};
}  // namespace bw::game
