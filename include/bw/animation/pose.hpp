// Ordered local layers retain source channel masks and independently supplied
// sample clocks. Both phases I.1 (#ri-explicit), E.4 (#re-design-invariants),
// MEM.9 and CDSA.32 validate the entire borrowed request before publication,
// retain immutable masks at preparation and reuse per-pose additive scratch.
// See alicia-animation-layers.md for order, equations and reference comparison.
// Compatible instances may share prepared keys: absent whole tracks retain
// the evaluating pose's own skeleton bind transform. Preparation defaults are
// used only by standalone clip endpoint/root queries; game consumers validate
// authored reference/root tracks before sharing those semantics. Both phases
// E.4/MEM.9 preserve the bind invariant without per-frame allocation. See
// prepared-animation-catalog.md.
// Pose modifiers use explicit local-basis or world-transform batches. Local
// basis overrides preserve sampled translation and precede hierarchy
// composition; world overrides preserve untouched descendant local
// relationships. I.1/E.4 validate space, indices, duplicate ownership and
// finite values before atomic publication. MEM.9 uses construction-time
// scratch; no per-frame allocation. Both MCP phases consulted;
// player-direction-reference.md records source and pinned Unreal
// SafeSetCSBoneTransforms comparison. Root extraction uses endpoint-inclusive
// cyclic translation and pose locking when a game movement owner consumes that
// delta. I.1 (#ri-explicit) makes the operation explicit; E.4
// (#re-design-invariants) rejects invalid intervals; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) allocates nothing
// during extraction. Both design and implementation phases consulted. Shared
// skeletal clip preparation and instance pose evaluation. PreparedClip owns
// decoded immutable keys and track constants; source views need survive only
// prepare(). SkeletonPose borrows an immutable skeleton through its entire
// lifetime and owns reusable local/component/skin buffers. One owner thread
// mutates a pose; prepared clips may be read concurrently. This layer neither
// selects gameplay actions nor integrates actor motion/contact.
// Algorithm: validate and decode keys once; preserve absent-track bind values
// and present-track constant channels; prepare parent-before-child order; map
// explicit elapsed time to authored frame units; sample vector/quaternion
// channels; compose local matrices with parents and actor transform; multiply
// inverse bind for skin output while preserving unskinned attachment poses.
// Preparation costs O(keys + bones), storage O(keys + bones). Evaluation costs
// O(channels * log keys + bones), with no allocations or archive access.
// All matrices use the original row-vector convention; native palette consumers
// transpose when forming dot-product rows. Invalid inputs throw before output
// publication. Different bone counts and nonfinite time/world matrices reject.
// Callers must establish semantic skeleton/track compatibility; equal counts
// alone do not establish matching bone identity.
// Both phases: cpp-guidelines I.11 (#ri-raw) separates borrowed skeleton
// lifetime from owned keys; cpp-performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// decodes once for contiguous runtime reads; CACHE.4
// (guidelines/cache-layout/CACHE.4-choose-aos-soa-aosoa-by-access-pattern.md)
// keeps per-bone fields together because all are consumed by transform
// evaluation. Implementation SL.con.3 (#rsl-bounds) validates encoded regions
// before prepared sampling. See actors-and-animation.md and
// skeletal-animation-baseline.md.
#pragma once

#include "bw/formats/animation_definition.hpp"
#include "bw/math/matrix.hpp"

namespace bw::animation {
using Matrix = math::Matrix4<float>;
inline constexpr Matrix kIdentityMatrix{
    {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};

struct ClipRootStep {
  double previous_seconds{}, seconds{}, signed_tick{};
};
class PreparedClip {
 public:
  static PreparedClip prepare(const formats::MotionDefinition& motion,
                              const formats::SkeletonDefinition& skeleton,
                              const std::shared_ptr<memory::Domain>& domain,
                              std::optional<bool> repeating = std::nullopt);
  std::size_t bones() const { return tracks_.size(); }
  float duration_frames() const { return duration_frames_; }
  float frames_per_second() const { return frames_per_second_; }
  bool repeating() const { return repeating_; }
  std::uint32_t channel_mask(std::size_t bone) const {
    return tracks_.at(bone).channel_mask;
  }
  double duration_seconds() const;
  // Sample the local endpoint independently of looping playback. Consumers
  // can prepare semantic reference bases without advancing a live pose.
  // O(log keys), no allocation; an invalid bone index throws. I.1/E.4/MEM.9.
  formats::SkeletonTransform end_transform(std::size_t bone) const;
  // Sample an already wrapped/clamped controller time, including the endpoint.
  // Relative root extraction uses source unit scale and one observed wrap:
  // add endpoint translation to current (forward wrap) or previous (reverse),
  // then current * inverse(previous). It does not invent multi-cycle rotation.
  // Finite in-range clocks required; singular roots reject. I.1/E.4/MEM.9,
  // both phases, preserve explicit controller ownership and allocation-free
  // work.
  formats::SkeletonTransform local_transform(std::size_t bone,
                                             double sampled_seconds) const;
  Matrix root_relative_transform(const ClipRootStep& step) const;
  // Setup-only guard for consumers caching a local reference. Compare every
  // authored key component; O(keys for this bone), no allocation. Conservative
  // equality rejects varying representations rather than silently freezing
  // them.
  bool has_constant_local_transform(std::size_t bone) const;
  // Extract translation between nonnegative absolute clip times. Repeating
  // clips accumulate complete endpoint-inclusive cycles; no wrap discontinuity.
  // O(log keys), no allocation. This does not mutate a pose or apply movement.
  std::array<float, 3> root_translation_delta(double from_seconds,
                                              double to_seconds) const;

 private:
  friend class SkeletonPose;
  struct Channel {
    std::size_t first{}, count{};
  };
  struct Track {
    formats::SkeletonTransform constant;
    std::array<Channel, 3> channels;
    bool bind_default{};
    std::uint32_t channel_mask{};
  };
  explicit PreparedClip(const std::shared_ptr<memory::Domain>& domain);
  void prepare_channel(Channel& destination,
                       const formats::MotionChannel& source,
                       std::size_t components, std::size_t component_offset,
                       bool whole);
  formats::SkeletonTransform sample(
      std::size_t bone, float frame,
      const formats::SkeletonTransform* bind_default = nullptr) const;
  memory::Vector<Track> tracks_;
  memory::Vector<float> times_;
  memory::Vector<std::array<float, 4>> values_;
  float duration_frames_{}, frames_per_second_{};
  bool repeating_{};
  std::size_t root_bone_{};
};

struct BoneLocalBasisOverride {
  std::size_t bone{};
  std::array<float, 4> rotation{0, 0, 0, 1};
  std::array<float, 3> scale{1, 1, 1};
};
struct BoneWorldOverride {
  std::size_t bone{};
  Matrix world{kIdentityMatrix};
};
// Contributions borrow immutable clips only during evaluation. A channel's
// chain is ordered oldest to newest; its oldest weight is implicitly one.
// Chains contain at most 32 contributors. Later weights are finite in [0,1].
// Distinct channels are strictly ascending
// in [0,31]. Additive chains ignore weights, matching the recovered evaluator.
// See alicia-animation-layers.md; callers own clocks, interruption and events.
struct PoseContribution {
  const PreparedClip* clip{};
  double seconds{};
  float weight{1};
};
struct PoseLayer {
  std::uint32_t channel{};
  std::span<const PoseContribution> contributions;
};
struct PoseLayers {
  std::span<const PoseLayer> ordinary, additive;
};
// Optional instance-owned local snapshot for disappearance of higher ordinary
// channels. Active channels above zero refresh their admitted-bone mask and
// post-additive locals before basis modifiers/hierarchy. Without them, weight
// decays before masked current-to-snapshot blending. The caller supplies the
// decay rate (source game policy is 3/second) and game delta. clear discards a
// snapshot on explicit instance reset. One owner thread; no cross-skeleton
// reuse unless the caller establishes semantic bone compatibility. E.4 and
// MEM.9, consulted in both design and implementation, require atomic
// pose/snapshot publication and construction-sized pending buffers, with
// O(bones) work and no allocation during evaluation. See
// alicia-animation-layers.md.
class PoseReleaseState {
 public:
  PoseReleaseState(std::size_t bones,
                   const std::shared_ptr<memory::Domain>& domain);
  PoseReleaseState(const PoseReleaseState&) = delete;
  PoseReleaseState& operator=(const PoseReleaseState&) = delete;
  PoseReleaseState(PoseReleaseState&&) noexcept = default;
  PoseReleaseState& operator=(PoseReleaseState&&) noexcept = default;
  ~PoseReleaseState() = default;
  void clear();
  // Preserve published snapshot/weight during a compatible appearance swap.
  // Equal bone capacity is required; semantic compatibility is caller-owned.
  // E.4/MEM.9 (both phases): reject mismatches before copying into existing
  // storage; O(bones), no allocation, source and destination remain
  // independent.
  void copy_from(const PoseReleaseState& source);
  float weight() const { return weight_; }

 private:
  friend class SkeletonPose;
  memory::Vector<formats::SkeletonTransform> snapshot_, pending_snapshot_;
  memory::Vector<std::uint8_t> mask_, pending_mask_;
  float weight_{};
};
struct PoseReleaseStep {
  PoseReleaseState* state{};
  double seconds{};
  float decay_per_second{};
};
// Identity consumes the first hierarchy root completely after composition.
// Translation locking preserves the legacy per-sample frame-zero position.
enum class RootPosePolicy : std::uint8_t {
  animated,
  lock_translation,
  identity
};
// Borrowed pre-hierarchy chain correction. Indices are distinct and ordered
// ancestors, excluding the consumed root; 2..8 nodes including a terminal
// direction marker. Target is a finite unit model-component direction;
// weight is [0,1]. Authored marker scale is retained by the original halfway
// quaternion formula. All but the terminal receive distributed corrections.
// Empty chain disables the modifier. No allocations, O(bones + chain ancestry).
// Source5e3ba0 and pinned Unreal LookAt comparison: raised-aim-posture.md.
// E.4/MEM.9 (both phases): validate/stage before publishing, fixed scratch.
struct AimChainStep {
  std::span<const std::size_t> bones;
  math::Vector3<float> target{0, 0, 1};
  float weight{};
};
class SkeletonPose {
 public:
  SkeletonPose(const formats::SkeletonDefinition& skeleton,
               const std::shared_ptr<memory::Domain>& domain);
  ~SkeletonPose() = default;
  SkeletonPose(const SkeletonPose&) = delete;
  SkeletonPose& operator=(const SkeletonPose&) = delete;
  SkeletonPose(SkeletonPose&&) noexcept = default;
  SkeletonPose& operator=(SkeletonPose&&) noexcept = default;
  // Frame selection is explicit; no hidden real-time clock or actor movement.
  // Root/world translation comes from the game movement owner, not this clip.
  void evaluate(const PreparedClip& clip, double elapsed_seconds,
                const Matrix& actor_world = kIdentityMatrix,
                bool lock_root_translation = false,
                std::span<const BoneLocalBasisOverride> basis_overrides = {});
  // Blend two complete local poses before hierarchy composition. Game policy
  // owns transition duration and selection; alpha is finite and in [0,1].
  void blend(float alpha, const PreparedClip& from, double from_seconds,
             const PreparedClip& to, double to_seconds,
             const Matrix& actor_world = kIdentityMatrix,
             bool lock_root_translation = false,
             std::span<const BoneLocalBasisOverride> basis_overrides = {});
  // Evaluate ordered masked override chains, then compose masked additive
  // deltas in local space. Missing ordinary tracks use this skeleton's bind;
  // missing additive tracks do not contribute. Unclaimed bones use bind pose.
  // Translation locking occurs per sample. Identity removes the first hierarchy
  // root after layers, release snapshots and local overrides, because the actor
  // already consumed its full relative transform. No allocations; invalid
  // inputs retain all published transforms. Cost O(bones * contributors * log
  // keys + bones). Design I.1/E.4 expose ordering/failure; MEM.9/CDSA.32
  // prepare masks once.
  void evaluate_layers(
      const PoseLayers& layers, const Matrix& actor_world = kIdentityMatrix,
      RootPosePolicy root_policy = RootPosePolicy::animated,
      std::span<const BoneLocalBasisOverride> basis_overrides = {},
      const PoseReleaseStep& release = {}, const AimChainStep& aim = {});
  // Borrow one unique override per bone for this call. Override targets are
  // absolute world transforms; untouched descendants retain local animation.
  // O(bones + overrides), no allocation; invalid batches leave outputs intact.
  // Local samples stay unchanged for later evaluation/secondary-input
  // inspection.
  void apply_world_overrides(std::span<const BoneWorldOverride> overrides);
  void bind_pose(const Matrix& actor_world = kIdentityMatrix);
  // Sampled SRT before matrix-space chain correction; world/skin outputs
  // include the final chain pose. Secondary world publication retains those
  // final matrix locals for untouched descendants.
  std::span<const formats::SkeletonTransform> local_transforms() const {
    return local_;
  }
  std::span<const Matrix> bone_transforms() const { return world_; }
  std::span<const Matrix> skin_transforms() const { return skin_; }

 private:
  void evaluate_ordinary_layers(std::span<const PoseLayer> layers,
                                bool lock_root_translation,
                                PoseReleaseState* release);
  void evaluate_additive_layers(std::span<const PoseLayer> layers,
                                bool lock_root_translation);
  formats::SkeletonTransform layer_sample(const PreparedClip& clip,
                                          std::size_t bone, float frame,
                                          bool lock_root_translation) const;
  void compose(const Matrix& actor_world, const AimChainStep& aim = {});
  void apply_aim_chain(const AimChainStep& aim);
  void override_local_basis(std::span<const BoneLocalBasisOverride> overrides);
  const formats::SkeletonDefinition* skeleton_;
  std::size_t root_bone_{};
  memory::Vector<std::size_t> order_, override_indices_;
  Matrix actor_world_{kIdentityMatrix};
  memory::Vector<formats::SkeletonTransform> local_;
  memory::Vector<Matrix> world_, skin_;
  memory::Vector<Matrix> local_matrices_, pending_local_matrices_;
  memory::Vector<formats::SkeletonTransform> pending_local_;
  memory::Vector<formats::SkeletonTransform> additive_local_;
  memory::Vector<Matrix> pending_world_, pending_skin_;
};
// Composition is shared CPU behavior; the native renderer receives the final
// palette. No model/actor dependency or native graphics type enters this API.
using math::multiply;
Matrix transform_matrix(const formats::SkeletonTransform& transform);
}  // namespace bw::animation
