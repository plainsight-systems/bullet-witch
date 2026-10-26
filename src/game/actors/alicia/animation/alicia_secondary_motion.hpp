// Alicia's authored secondary-motion binding and clock. The reusable engine
// solver owns mechanics; this adapter owns the original PW node topology,
// six body capsules and costume compatibility. Construction copies/validates
// control-point data and preallocates all frame storage. Unsupported skeletons
// or missing weighted anchors fail before publication. Caller retains no byte
// dependency after construction and serializes reset/advance with pose updates.
// Algorithm: validate named secondary/body bones and control slots, initialize
// source bind/rest constraints, then evaluate animated anchors and advance once
// per supplied game delta; return borrowed world overrides for the pose owner.
// O(points*influences + nodes*capsules) update, no steady-state allocation.
// Both phases: Core I.1 (#ri-explicit), SL.con.3 (#rsl-bounds); performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md). Source
// topology, numerical constants and lifecycle: costume-secondary-motion.md.
#pragma once
#include "bw/animation/secondary_motion.hpp"
#include "bw/content/formats/model.hpp"
namespace bw::game {
class AliciaSecondaryMotion {
 public:
  AliciaSecondaryMotion(const formats::ModelDefinition& model,
                        std::span<const std::uint8_t> control_points,
                        const std::shared_ptr<memory::Domain>& domain);
  void reset(const animation::SkeletonPose& bind_pose);
  std::span<const animation::BoneWorldOverride> advance(
      float delta_seconds, float ground_height,
      const animation::SkeletonPose& clean_pose);
  const animation::SecondaryMotion& simulation() const { return motion_; }

 private:
  formats::ControlPointDefinition control_points_;
  memory::Vector<animation::Matrix> frames_;
  animation::SecondaryMotion motion_;
  float elapsed_{};
  bool first_{true};
};
}  // namespace bw::game
