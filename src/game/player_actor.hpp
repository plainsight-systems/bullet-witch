// Actor publication borrows WorldPresentation only; it cannot upload, bind or
// retire mission resources. Both-phase C.121 (#rh-abstract), I.11 (#ri-raw),
// COPY.5 (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// enforce the existing owner split without changing frame work or lifetimes.
// Weapon publication composes activation-local and model-render half-turns
// before the completed body attachment. I.1 names both source spaces; MEM.9
// keeps their fixed matrix composition outside allocation paths.
// Attached weapon models suppress the authored root after local animation
// blending, before hierarchy composition. That root is an export-space frame,
// not another offset from the completed hand. Both-phase I.1/MEM.9 retain the
// explicit source root policy and reuse the prepared pose without allocation.
// Facing reads the raw authored reference independently from displacement
// correction. Both-phase I.1 (#ri-explicit) and MEM.9 expose distinct consumers
// using immutable prepared samples without allocation or pose publication.
// Aim chain indices are prepared per appearance; stable playback owns the
// quarter-second weight and retained component-space direction. Explicit unit
// targets are validated before mutation, and only consumed motion time advances
// the blend. Pose publication and paused costume preparation never tick it.
// Both-phase I.1/E.4 and MEM.9 keep spaces, failure and fixed storage explicit.
// Source action plans resolve native bindings once per request and keep
// unavailable publications explicit. Independent body/upper events run after
// model shape/pose evaluation. Facial banks and retained CPU shape positions
// live at stable appearance addresses; paused costume adoption copies logical
// bank state and rebinds the stable event sink only after renderer publication.
// Registered visibility listeners mutate weapon group bits through retained
// draw visibility. Both-phase I.1/E.4 and MEM.9 keep identity/phase/lifetime
// contracts explicit and all per-frame storage prepared. See
// facial-animation.md and alicia-action-policy.md; unimplemented audio/effects
// stay reported. All named body/weapon motions share immutable prepared
// catalogs across compatible costume replacements. Compatibility aliases retain
// ordered shared repeat policy; active traversal stores catalog indices, never
// copied keys. Absent tracks evaluate against the current costume's bind
// defaults. Core I.11/E.4 and performance CDSA.32/MEM.9 are applied in both
// phases; see character_animations.hpp and prepared-animation-catalog.md. A
// paused appearance candidate prepares against its own skeleton defaults and
// current playback state. Atomic renderer publication precedes nonthrowing
// adoption into this stable address; old resource owners retire afterward.
// Weapon review replaces only its weapon catalog and neutral playback,
// retaining the frozen body image until resume; old scoped audio retires only
// on commit. Candidate setup may allocate, but does not advance game clocks.
// E.4
// (#re-design-invariants) validates compatibility before commit; I.11 bounds
// both sets of borrowed mission assets through their transaction.
// Secondary pose integration owns one persistent Alicia solver. Construction
// copies control points and resets from bind pose in the same rendered world
// space. Accumulate consumed game time across movement substeps; on publication
// evaluate a clean body pose, advance secondary state once, apply declared bone
// overrides, then attach the weapon and publish palettes. Zero-time publication
// does not integrate. Pause must not consume game time. No corrected secondary
// output is fed back as an animated anchor. Both-phase I.11 (#ri-raw) retains
// mission asset borrowing; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) preallocates solver/pose storage.
// Selected Alicia model/animation ownership. Borrow mission-owned resident
// definitions and support through destruction. Own prepared locomotion clips,
// reusable skeleton poses and draw palettes in the injected domain; one owner
// thread. Resolve named clips/attachment once, advance explicit clip clocks,
// extract the current controller's root displacement for game movement, then
// consume its contact-corrected world pose. Remove the consumed root transform
// in the skeleton, evaluate the body and attached weapon, and publish retained
// palettes. Legacy presentation-only advance keeps root motion in the pose.
// Setup costs O(selected keys + bones + draw palette references); update costs
// O(channels * log keys + bones + palette references), without allocations,
// file reads, geometry upload or material rebuilding. Clip transitions require
// an explicit game-selected duration; no invented automatic jump trajectory.
// Both phases: I.11 (#ri-raw) retains mission borrowing; CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// prepares clips and palette mappings once; CACHE.4
// (guidelines/cache-layout/CACHE.4-choose-aos-soa-aosoa-by-access-pattern.md)
// groups fields consumed together. Implementation SL.con.3 (#rsl-bounds)
// validates source-to-palette indices. See actors-and-animation.md and
// alicia-animation-bindings.md for reconstructed scope and native evidence.
// Selected controller root transforms and current Waist_direction samples
// drive movement separately from the render-only half turn. The shared action
// owner retains source-sized body/upper/additive/weapon rings and one event
// timeline per selected controller; outgoing clips contribute poses only.
// Costume-local release snapshots and secondary poses consume game delta once
// at publication. Cancellation uses authored timeline entry/reset, rather than
// a threshold approximation. Both-phase E.4 (#re-design-invariants) validates
// selection and MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// fixes runtime storage. See alicia-animation-layers.md and
// motion-event-timeline.md.
#pragma once
#include "alicia_action_policy.hpp"
#include "alicia_secondary_motion.hpp"
#include "alicia_support_arm.hpp"
#include "bw/animation/pose.hpp"
#include "bw/audio/output.hpp"
#include "bw/content/character_support.hpp"
#include "camera/initial_camera.hpp"
#include "character_animations.hpp"
#include "character_motion_player.hpp"
#include "resident_model.hpp"

namespace bw::scene {
class StaticCollision;
}
namespace bw::game {
class PreparedCharacterAudio;
class SourceRandom;
struct PlayerActorAudioSource {
  std::reference_wrapper<const PreparedCharacterAudio> prepared;
  std::reference_wrapper<audio::CueOutput> output;
  std::reference_wrapper<const scene::StaticCollision> collision;
  std::reference_wrapper<SourceRandom> random;
};
struct PlayerActorSource {
  ResidentModelView body, weapon;
  std::reference_wrapper<const content::CharacterSupport> support,
      weapon_support;
  ScriptedPlayerPose placement;
};
enum class PlayerMotion : std::uint8_t {
  stand,
  walk,
  run,
  fall,
  hard_land,
  evade_forward_start,
  evade_forward_air,
  evade_forward_land,
  evade_backward_start,
  evade_backward_air,
  evade_backward_land,
  evade_left_start,
  evade_left_air,
  evade_left_land,
  evade_right_start,
  evade_right_air,
  evade_right_land
};
enum class CharacterMotionBank : std::uint8_t {
  body,
  upper_body,
  additive,
  weapon
};
struct ActionPresentationAdmission {
  std::size_t admitted{}, unavailable{};
};
struct CharacterPresentationStatus {
  // Source channels with no concrete consumer are counted, never acknowledged
  // as successful sound, facial, visibility or combat behavior.
  std::array<std::uint64_t, formats::kMotionEventChannels> unresolved_entries{};
};
enum class PlayerAppearanceChange : std::uint8_t {
  costume,
  weapon,
  live_weapon
};
class PlayerActor {
 public:
  PlayerActor(PlayerActorSource source,
              const std::shared_ptr<memory::Domain>& domain,
              const std::function<void()>& progress = {});
  // Prepare a costume candidate with the same playback/physical state. Rebuild
  // bindings against its skeleton; reject incompatible names/hierarchy/weapon.
  // Caller keeps both mission resource owners alive until renderer publication.
  PlayerActor(PlayerActorSource source, const PlayerActor& current,
              const std::shared_ptr<memory::Domain>& domain);
  // Weapon review prepares a new class-specific catalog and neutral playback.
  // Body/support identity must be unchanged. Preserve the frozen displayed body
  // and physical transform until resume; no current audio/events are touched.
  // live_weapon instead shares body/action/event/aim clocks and prepares only
  // the selected weapon rest (1 lowered, 2 raised). Preparation never advances
  // or resets the current owner. The source switch upper request follows
  // commit. Publication adopts the candidate before MissionLoading retires old
  // assets.
  PlayerActor(PlayerActorSource source, const PlayerActor& current,
              const std::shared_ptr<memory::Domain>& domain,
              PlayerAppearanceChange change, bool weapon_raised = false);
  ~PlayerActor();
  PlayerActor(const PlayerActor&) = delete;
  PlayerActor& operator=(const PlayerActor&) = delete;
  PlayerActor(PlayerActor&&) = delete;
  // After transactional renderer replacement, preserve this object's address
  // while adopting the fully prepared candidate. No allocation or failure;
  // old appearance storage retires here, before its mission resources.
  void adopt_appearance(PlayerActor&& candidate) noexcept;
  // Bind prepared mission audio once before frame dispatch. Collaborators
  // outlive this actor; stable playback owns independent body/upper handles
  // and shared voice arbitration across costume changes. Allocation is setup
  // only. Listener updates copy fixed values; teardown stops owned cues before
  // the mission output is destroyed.
  void bind_audio(PlayerActorAudioSource source,
                  const std::shared_ptr<memory::Domain>& domain);
  void set_audio_listener(const audio::Listener& listener);

  // Game policy selects motion; selecting an airborne clip cannot move the
  // root without the movement owner consuming the displacement.
  void select_motion(PlayerMotion motion, float blend_seconds);
  // Prepared game action requests select explicit catalog identities; the bank
  // owns source clocks and events. This does not admit a spell, hit or mission
  // action. Action policies own those capabilities and subsequent transitions.
  animation::PlaybackAdmission request_animation(
      CharacterMotionBank bank, const CharacterMotionRequest& request);
  void clear_animation(CharacterMotionBank bank);
  // Explicit presentation-review reset; no gameplay outcome. Retire scoped
  // receivers, select neutral prepared owners and clear pending root/aim state.
  // Fixed storage, valid installed neutral bindings required (I.1/E.4/MEM.9).
  void reset_animation_presentation();
  // Direct review command, distinct from FAC event admission. Returns false
  // for missing expression or shape capability; no synthetic geometry.
  bool has_facial_shapes() const noexcept;
  bool preview_facial(std::size_t expression);
  // Apply source-ordered presentation requests; unavailable authored slots
  // remain explicit and do not suppress independent valid requests. No combat
  // or spell admission is inferred. Catalog/clock identities resolve in O(1).
  ActionPresentationAdmission apply_action_plan(const ActionMotionPlan& plan);
  const animation::ClipPlaybackState* animation_state(
      CharacterMotionBank bank) const;
  const CharacterPresentationStatus& presentation_status() const;
  // A source motion request scales clip playback independently from the
  // real-time blend duration. Outgoing clips retain their selected rate through
  // a transition. Zero is a valid held pose; negative rates reject.
  void set_playback_rate(float rate);
  // Model-component unit direction, after the source target conversion. A
  // disabled request retains the last target while its weight fades out.
  void set_aim_posture(const math::Vector3<float>& direction, bool enabled);
  // Game action owner grants a grip only while ordinary raised input is
  // permitted. Exclusive actions revoke it immediately, independently of the
  // torso's fading aim weight. Upper free-hand actions impose a second veto.
  // Both-phase I.1/MEM.9: explicit value permission, no controller borrowing.
  void set_support_arm_permission(bool permitted) noexcept;
  float aim_posture_weight() const;
  // Current selected body controller reference, before the pose resets it.
  animation::Matrix movement_reference() const;
  // Source raised facing consumes the current raw Waist_direction SRT matrix.
  // Unlike displacement's reference, do not negate forward Z or rebuild its
  // basis; authored scale/translation remain intact. No clock/state mutation.
  animation::Matrix authored_facing_reference() const;
  // Parent update consumes the preceding child-model interval through the
  // CURRENT selected controller's authored reference. Fresh models start with
  // identity movement; selecting a new clip does not clear this cache.
  math::Vector3<float> pending_root_displacement() const;
  // Duration of the retained raw interval, preserved across selection and
  // costume changes. Derived root velocity uses this interval, not the next
  // parent's possibly different timestep. Zero denotes fresh/reset identity.
  double pending_root_seconds() const { return pending_root_seconds_; }
  // Child-model phase: advance independent clocks and publish the raw root
  // interval for the NEXT parent update. The returned transformed displacement
  // is available to presentation diagnostics; physical controllers consume the
  // pending interval before calling this method. Pose publication removes the
  // consumed root transform. See actor-model-clock-order.md.
  std::array<float, 3> advance_motion(double delta_seconds);
  // Current controller's relative root orientation; translation is zero.
  // Borrowed until the next motion advance or appearance replacement. The
  // movement owner postmultiplies its preceding orientation after translation.
  const animation::Matrix& relative_root_rotation() const {
    return relative_root_rotation_;
  }
  // The live source actor places its model at negative effective body radius;
  // the constructor offset is superseded before publishing a physical pose.
  void publish_pose(const ScriptedPlayerPose& placement, float body_radius,
                    render::WorldPresentation& output);
  void publish_pose(const ScriptedPlayerPose& placement,
                    const animation::Matrix& orientation, float body_radius,
                    render::WorldPresentation& output);
  // Complete one child-model update before another parent step: corrected
  // transform, pose/secondary/attachments, events, then ring retirement. Does
  // not advance clocks again or submit GPU buffers. E.4 makes the phase
  // invariant explicit; MEM.9 retains allocation-free prepared storage.
  void complete_model_step(const ScriptedPlayerPose& placement,
                           const animation::Matrix& orientation,
                           float body_radius);
  // Publish the latest completed snapshot once after simulation subdivisions.
  // No pose evaluation, event dispatch or clock advancement occurs here.
  void publish_completed_pose(render::WorldPresentation& output);
  bool motion_finished() const;
  // Authored cancellation entry stays latched until the current receiver
  // resets. Game states decide which exits permit cancellation; a marker does
  // not cancel takeoff by itself. This query reads the prepared timeline latch.
  // Both-phase E.4/MEM.9 preserve explicit state and fixed steady-state
  // storage.
  bool motion_cancellable() const;
  PlayerMotion motion() const { return current_; }
  void advance(double delta_seconds, render::WorldPresentation& output);
  const PlayerActorSource& source() const { return source_; }
  const CharacterAnimations& body_animations() const { return *body_catalog_; }
  const CharacterAnimations& weapon_animations() const {
    return *weapon_catalog_;
  }
  std::span<const render::WorldJointMatrix> draw_palette(
      bool weapon, std::size_t draw) const;
  bool includes_draw(bool weapon, std::size_t draw) const;
  // Admission is stable; listener-driven visibility changes retained draws.
  bool draw_visible(bool weapon, std::size_t draw) const;
  // Source model render matrix before skeleton/root animation. Lighting samples
  // the group center in this space, independently of animated root tracks.
  animation::Matrix model_transform(bool weapon) const;
  std::span<const animation::Matrix> body_bones() const {
    return body_pose_.bone_transforms();
  }

 private:
  PlayerActor(PlayerActorSource source,
              const std::shared_ptr<memory::Domain>& domain,
              const PlayerActor* shared_appearance,
              const std::function<void()>& progress,
              PlayerAppearanceChange change = PlayerAppearanceChange::costume,
              bool weapon_raised = false);
  PlayerActor& operator=(PlayerActor&&) noexcept = default;
  static constexpr std::size_t kPaletteCapacity = 32, kAimChainCapacity = 6;
  static constexpr std::size_t kMotionCount = 17;
  static constexpr std::size_t kWeaponMotionCount = 7;
  struct Palette {
    render::WorldModelHandle model;
    std::size_t draw{}, count{};
    bool weapon{};
    std::array<std::size_t, kPaletteCapacity> bones{};
    std::array<render::WorldJointMatrix, kPaletteCapacity> matrices{};
  };
  const animation::PreparedClip& clip(PlayerMotion motion) const;
  void prepare_palettes(bool weapon);
  void evaluate_body();
  void evaluate();
  void evaluate_attachment_palettes();
  void evaluate_weapon_pose();
  void fill_draw_palettes();
  void finish_model_step();
  CharacterMotionPlayer& motion_bank(CharacterMotionBank bank) const;
  void publish_visibility(render::WorldPresentation& output);
  struct Playback;
  struct ShapeState;
  struct AudioState;
  PlayerActorSource source_;
  animation::Matrix model_world_;
  animation::Matrix relative_root_rotation_{animation::kIdentityMatrix};
  math::Vector3<float> pending_root_translation_{};
  double pending_root_seconds_{};
  std::shared_ptr<const CharacterAnimations> body_catalog_, weapon_catalog_;
  std::array<std::size_t, kMotionCount> motion_indices_{};
  std::array<std::optional<std::size_t>, kMotionCount> motion_parameters_;
  std::size_t weapon_stand_{};
  std::array<std::optional<std::size_t>, kWeaponMotionCount> weapon_slots_{};
  animation::SkeletonPose body_pose_, weapon_pose_;
  animation::PoseReleaseState body_release_;
  std::optional<AliciaSecondaryMotion> secondary_motion_;
  std::optional<AliciaSupportArm> support_arm_;
  memory::Vector<animation::BoneWorldOverride> modifier_overrides_;
  double secondary_delta_{};
  animation::BoneLocalBasisOverride direction_reference_;
  memory::Vector<Palette> palettes_;
  memory::Vector<render::WorldPoseUpdate> updates_;
  memory::Vector<render::WorldVisibilityUpdate> visibility_updates_;
  std::uint32_t published_weapon_groups_{};
  std::size_t attachment_bone_{};
  std::array<std::size_t, kAimChainCapacity> aim_chain_{};
  std::size_t aim_chain_size_{};
  float sound_probe_half_extent_{};
  PlayerMotion current_{PlayerMotion::stand};
  bool consume_root_motion_{};
  // Costume candidates share clocks/effects without ticking. Weapon review
  // candidates prepare separate neutral playback and transfer audio on commit.
  std::shared_ptr<ShapeState> shapes_;
  std::shared_ptr<Playback> playback_;
  // Selected weapon clocks borrow the stable body playback sink. Retire them
  // before that sink and their catalog; live switching shares body clocks.
  std::shared_ptr<CharacterMotionPlayer> weapon_playback_;
  std::uint32_t weapon_rest_slot_{1};
  bool live_weapon_candidate_{};
};
}  // namespace bw::game
