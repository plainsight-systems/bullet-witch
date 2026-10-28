// Immutable source-selected NPC initial body/weapon motion.
// Mission residency prepares this owner once per target appearance and motion
// owner; actors borrow its clip and sample into separate retained local
// buffers. Input model/support views need survive preparation only. The result
// owns decoded clip keys and optional shared-human retarget data. No actor
// clocks, AI decisions, resource uploads, root suppression or world transforms
// live here. Resolve grounded body slot-one stand_l or weapon slot-zero
// defaultmotion from an explicit supported owner, validate direct bone
// identities or prepare the original human map, then prepare repeat-enabled
// keys. Sample already wrapped/clamped time into caller scratch; the actor
// applies root policy and publishes its own pose. Missing/ambiguous
// publications or incompatible skeletons throw; no silent bind-pose fallback.
// Setup O(publications + keys + bone mapping); runtime O(bones * log keys), no
// allocation. One immutable owner may serve many actors. Both
// design/implementation Core I.11 (#ri-raw), R.14 (#rr-ap) and performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) distinguish
// shared prepared inputs from bounded mutable instance scratch. See
// npc-shared-motion.md.
#pragma once

#include "bw/content/formats/customization.hpp"
#include "game/actors/animation/shared_motion_pose.hpp"
#include "game/content/original/character_support.hpp"

namespace bw::game {
// Source body dimensions are separate from mesh scale or motion-owner aliases.
// Read the named actor's immutable customization node at load time. The caller
// owns this small value and applies live health/radius policy; no tree borrow
// survives parsing. Missing or invalid authored dimensions throw.
struct NpcInitialBodySettings {
  float radius{}, standing_height{}, alternate_height{}, dodge_threshold{};
  static NpcInitialBodySettings parse(
      const formats::CustomizationDefinition& customization,
      std::string_view model_family);
};
class NpcInitialMotion {
 public:
  static NpcInitialMotion prepare(
      const formats::ModelDefinition& target,
      const formats::ModelDefinition& owner,
      const content::CharacterSupport& support, std::string_view motion_owner,
      const std::shared_ptr<memory::Domain>& domain);
  NpcInitialMotion(const NpcInitialMotion&) = delete;
  NpcInitialMotion& operator=(const NpcInitialMotion&) = delete;
  NpcInitialMotion(NpcInitialMotion&&) noexcept = default;
  NpcInitialMotion& operator=(NpcInitialMotion&&) noexcept = default;
  ~NpcInitialMotion() = default;
  const animation::PreparedClip& clip() const { return clip_; }
  std::uint32_t binding() const noexcept { return binding_; }
  void sample(double sampled_seconds,
              std::span<formats::SkeletonTransform> destination) const;

 private:
  NpcInitialMotion(animation::PreparedClip clip,
                   std::optional<SharedMotionPose> mapping,
                   memory::Vector<formats::SkeletonTransform> direct_bind,
                   std::uint32_t binding)
      : clip_(std::move(clip)),
        mapping_(std::move(mapping)),
        direct_bind_(std::move(direct_bind)),
        binding_(binding) {}
  animation::PreparedClip clip_;
  std::optional<SharedMotionPose> mapping_;
  memory::Vector<formats::SkeletonTransform> direct_bind_;
  std::uint32_t binding_{};
};
}  // namespace bw::game
