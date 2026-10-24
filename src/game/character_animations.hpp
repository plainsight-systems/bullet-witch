// Prepared pose semantics distinguish ordinary local transforms from additive
// deltas and unknown representations before review admission. Resolve verified
// actor-specific ordinary/additive receiver slots once; unclassified resources
// and actors remain unknown, never ordinary by exclusion. Canonical publication
// identity must also match, so remapping a known slot grants no compatibility.
// Retain the one explicitly witnessed unbound delta publication without
// inferring a mode from a name suffix. Both-phase I.1 (#ri-explicit) exposes
// the compatibility boundary; CDSA.32 prepares one byte per clip for O(1)
// queries, shared with the immutable catalog. See animation-review-geometry.md.
// Game-side character publication adapter. The mission's CharacterSupport and
// its immutable archive bytes outlive this owner and all borrowed parameter /
// shape views. Own one prepared motion catalog and sorted publication indices;
// the actor's clocks, poses and secondary solver remain separate. Caller gives
// the exact support owner and ordered native aliases; no action-family names
// are inferred from suffixes. Independent parameter-only/shape publications
// remain addressable. Resolve exact compatibility basenames, reject duplicate
// publications, prepare shared clips once, then index borrowed metadata. Setup
// O(keys + publications log publications); runtime index access O(1), explicit
// name lookup O(log publications), no allocation. Costume candidates share this
// immutable owner only after their named skeleton compatibility is validated.
// Core I.11 (#ri-raw)/E.4 (#re-design-invariants) preserve mission borrowing
// and valid identities; performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-
// static-data-into-the-consumer-layout.md)/MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) separate setup from steady-state work.
// Both design and implementation phases consulted. See prepared-animation-
// catalog.md and alicia-animation-coverage.md.
#pragma once
#include "bw/animation/clip_catalog.hpp"
#include "bw/content/character_support.hpp"
#include "bw/formats/motion_events.hpp"
#include "motion_presentation.hpp"

namespace bw::game {
enum class MotionPoseSemantics : std::uint8_t { unknown, ordinary, additive };
class CharacterAnimations {
 public:
  CharacterAnimations(const content::CharacterSupport& support,
                      const formats::SkeletonDefinition& skeleton,
                      std::string_view owner, std::size_t slot_count,
                      std::span<const animation::ClipAliasRequest> aliases,
                      const std::shared_ptr<memory::Domain>& domain,
                      const std::function<void()>& progress = {});
  const animation::PreparedClipCatalog& motions() const { return motions_; }
  // Indexed exactly like motions(); the borrowed span survives this owner.
  // This describes pose representation, not gameplay/action admission.
  // Unknown entries remain inspectable but are not safe pose-bank requests.
  MotionPoseSemantics pose_semantics(std::size_t clip) const {
    return pose_semantics_.at(clip);
  }
  std::span<const MotionPoseSemantics> pose_semantics() const {
    return pose_semantics_;
  }
  // The first INT record controls transition duration, independently of the
  // event timeline. Negative entry / nonpositive exit retain caller policy.
  struct BlendFrames {
    std::int32_t entry{}, exit{};
  };
  struct Parameter {
    std::string_view name;
    std::span<const std::uint8_t> bytes;
    std::optional<std::uint32_t> cancellation_frame;
    formats::MotionEvents events;
    PreparedMotionPresentation presentation;
    std::optional<BlendFrames> blend_frames;
  };
  struct Shape {
    std::string_view name;
    std::reference_wrapper<const formats::ShapeAnimationDefinition> definition;
  };
  std::span<const Parameter> parameters() const { return parameters_; }
  std::span<const Shape> shapes() const { return shapes_; }
  const Parameter* parameter(std::string_view name) const;
  std::optional<std::size_t> parameter_index(std::string_view name) const;
  const Shape* shape(std::string_view name) const;
  std::optional<std::size_t> parameter_for_clip(std::size_t clip) const {
    return clip_parameters_.at(clip);
  }

 private:
  animation::PreparedClipCatalog motions_;
  memory::Vector<Parameter> parameters_;
  memory::Vector<Shape> shapes_;
  memory::Vector<std::optional<std::size_t>> clip_parameters_;
  memory::Vector<MotionPoseSemantics> pose_semantics_;
};
// Original global/PW slot mapping is compatibility policy, not a universal
// game-action enum. Missing motion slots remain absent even if parameters
// exist.
std::span<const animation::ClipAliasRequest> alicia_motion_aliases();
inline constexpr std::size_t kOriginalCharacterMotionSlots = 1108;
}  // namespace bw::game
