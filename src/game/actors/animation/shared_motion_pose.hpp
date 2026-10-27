// Prepared original shared-motion retargeting, separate from actor decisions.
// Preparation borrows target/owner skeletons and names only for the call, then
// owns target bind defaults and the original name mapping/translation scales/
// rotation corrections. The immutable result can serve multiple actor poses.
// Sampling borrows a compatible owner clip and caller-owned scratch for the
// call; the actor owns its clock, root policy and final SkeletonPose. Inputs
// must name every bone once and remain finite. Invalid preparation throws;
// sampling validates sizes/time first, but caller scratch is not a published
// pose and may be partially written on arithmetic failure.
// Algorithm: match target names to owner indices, prepare source hips/segment
// length factors and relative orientation differences; sample base-channel
// owner locals, apply corrections, retain target binds for unmatched bones.
// Cost: preparation O(target bones * owner bones), storage O(target bones);
// sampling O(target bones * log keys), no allocation or resource access.
// Both MCP phases: Core I.11 (#ri-raw) keeps ownership explicit; R.14 (#rr-ap)
// bounds borrowed arrays; performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) prepares immutable mapping before ticks.
// Source contract and pinned Unreal comparison: npc-shared-motion.md.
#pragma once

#include "bw/animation/pose.hpp"
#include "bw/content/formats/model.hpp"

namespace bw::game {
class SharedMotionPose {
 public:
  static SharedMotionPose prepare(
      const formats::SkeletonDefinition& target,
      std::span<const formats::ModelBoneName> target_names,
      const formats::SkeletonDefinition& owner,
      std::span<const formats::ModelBoneName> owner_names,
      const std::shared_ptr<memory::Domain>& domain);
  std::size_t bones() const { return entries_.size(); }
  // The actor supplies a wrapped/clamped clock in [0, clip duration]. Offset
  // is the original per-motion controller correction, not a guessed bind delta.
  void sample(const animation::PreparedClip& clip, double sampled_seconds,
              std::span<formats::SkeletonTransform> destination,
              math::Vector3<float> hips_offset = {}) const;

 private:
  struct Entry {
    std::optional<std::size_t> owner;
    formats::SkeletonTransform bind;
    math::Vector<float, 4> rotation{0, 0, 0, 1};
    float translation_scale{1};
  };
  explicit SharedMotionPose(const std::shared_ptr<memory::Domain>& domain)
      : entries_(memory::Allocator<Entry>(domain)) {}
  memory::Vector<Entry> entries_;
  std::size_t owner_bones_{};
  std::optional<std::size_t> hips_;
};
}  // namespace bw::game
