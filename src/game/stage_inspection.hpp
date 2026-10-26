// Implement the owner contract in the matching header. Design I.1/C.9 keeps
// authoritative state private; implementation E.6 (#re-raii) preserves cleanup
// on failed preparation. Both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps retained buffers
// fixed during admitted frames. Existing source algorithms and ordering remain.
// Transitional composition facade for existing mission callers. Simulation,
// retained scene presentation and explicit diagnostics have distinct owners;
// this class owns no actor, contact, lighting, clock or preview state itself.
// Construct simulation before its const presentation borrow and diagnostics;
// destroy in reverse order so draws retire before the actor/assets they borrow.
// Frame order: admission/parent-model simulation, environment observation,
// camera far-distance update, then view/light/material publication.
#pragma once
#include "mission_simulation.hpp"
#include "player_animation_review.hpp"
#include "stage_presentation.hpp"
namespace bw::game {
class StageInspection {
 public:
  // Playerless inspection requires an explicit placement; no mission default
  // is hidden in this reusable view. Player input owns its own placement.
  StageInspection(
      const world::WorldClock& clock, const MissionControl& control,
      render::WorldPresentation& output, StageViewSource stage,
      StageViewSource sky, const std::shared_ptr<memory::Domain>& domain,
      const std::optional<render::ViewPose>& camera_pose = std::nullopt,
      StageBackgroundSource background = {},
      const std::optional<PlayerActorSource>& player = std::nullopt,
      const content::LoadProgress& progress = {},
      StageCharacterLightSource light = {},
      const std::optional<ScriptedPlayerPose>& inspection_placement =
          std::nullopt);
  ~StageInspection();
  // Advance retained material curves and optional actor poses on the game
  // frame. Borrowed keys remain mission-owned; no geometry/sampler publication
  // or allocation occurs. Foreign/repeated frames reject before scene work;
  // control-only frames synchronize pause without advancing scene consumers.
  void advance(const world::WorldFrame& frame, StageTraversalInput input = {});
  const PlayerTargeting& targeting_state() const {
    return simulation_.targeting_state();
  }
  const PlayerMovementState* player_state() const {
    return simulation_.player_state();
  }
  // Accepted game messages reach the existing player lifetime. This is not
  // hardware/combat admission; callers fulfill reported scope/handoff needs.
  // Empty for a presentation-only scene without a physical player controller.
  AliciaActionOwner* player_actions() { return simulation_.player_actions(); }
  AliciaActionUpdate begin_player_scripted_jump(
      const AcceptedScriptedJump& request);
  const AliciaActionUpdate* player_action_status() const {
    return simulation_.player_action_status();
  }
  const InitialPlayerCamera* camera_state() const {
    return simulation_.camera_state();
  }
  // Borrow completed unskinned world transforms until the next advance or
  // scene destruction. Empty without a player. No copies, allocations or pose
  // evaluation; same owner-thread contract as player_state. I.1/MEM.9.
  std::span<const animation::Matrix> player_world_pose() const {
    const auto* player_ = simulation_.actor();
    return player_ ? player_->body_bones()
                   : std::span<const animation::Matrix>{};
  }
  // Borrow the selected prepared bank clock until the next action/update or
  // appearance replacement. Diagnostic observation never advances playback.
  const animation::ClipPlaybackState* player_animation_state(
      CharacterMotionBank bank) const {
    const auto* player_ = simulation_.actor();
    return player_ ? player_->animation_state(bank) : nullptr;
  }
  bool paused() const { return simulation_.paused(); }
  // Publish pause to the audio device before any paused resource transaction.
  void synchronize_pause();
  // Mission-owned output/prepared policy and session RNG outlive this view.
  // Bind once after actor admission; pause and listener updates follow the
  // same stage clock as its animation event owners.
  // Mission resources outlive this stage. Bind once before ordinary actions;
  // this handle can spend accepted actions, never reset a resource or clock.
  void bind_action_resources(PlayerActionResources resources);
  void bind_audio(const PreparedCharacterAudio& prepared,
                  audio::CueOutput& output, SourceRandom& random);
  // Explicit paused appearance transaction. Prepare/load candidate resources,
  // compatible clip bindings and current pose, atomically replace body draws,
  // then adopt without moving controller/camera or their clocks. Preparation
  // failure keeps the prior appearance; mission owners outlive both candidates.
  // This is the approved in-session costume improvement, not original Lua.
  void change_costume(MissionLoading& loading, unsigned costume);
  // Paused class transaction: candidate catalogs/draws before commit, neutral
  // action reset while physical position/camera/costume remain owned here.
  void change_weapon(MissionLoading& loading, unsigned weapon_class);
  // Normal accepted presentation switch: activate admitted weapon resources
  // without pausing traversal/audio; retain body/action/raised clocks, then
  // submit its source upper switch animation. No inventory/ammo outcome.
  void switch_weapon(MissionLoading& loading, unsigned weapon_class);
  const animation::PreparedClipCatalog* review_catalog(bool weapon) const {
    const auto* player_ = simulation_.actor();
    if (!player_) return nullptr;
    return weapon ? &player_->weapon_animations().motions()
                  : &player_->body_animations().motions();
  }
  // Same lifetime and indices as review_catalog(); no copied classification.
  std::span<const MotionPoseSemantics> review_pose_semantics(
      bool weapon) const {
    const auto* player_ = simulation_.actor();
    if (!player_) return {};
    return weapon ? player_->weapon_animations().pose_semantics()
                  : player_->body_animations().pose_semantics();
  }
  AnimationReviewResult review(AnimationReviewRequest request);
  std::string_view review_status() const { return review_.review_status(); }
  StageInspection(const StageInspection&) = delete;
  StageInspection& operator=(const StageInspection&) = delete;
  StageInspection(StageInspection&&) = delete;
  StageInspection& operator=(StageInspection&&) = delete;
  std::size_t selected_draws() const { return presentation_.selected_draws(); }
  std::size_t omitted_draws() const { return presentation_.omitted_draws(); }
  std::size_t sky_draws() const { return presentation_.sky_draws(); }
  std::size_t background_draws() const {
    return presentation_.background_draws();
  }
  std::size_t player_draws() const { return presentation_.player_draws(); }

 private:
  StageInspection(const world::WorldClock&, const MissionControl&,
                  render::WorldPresentation&, StageViewSource, StageViewSource,
                  const std::shared_ptr<memory::Domain>&,
                  const std::optional<render::ViewPose>&, StageBackgroundSource,
                  const std::optional<PlayerActorSource>&,
                  const content::LoadProgress&, StageCharacterLightSource,
                  const std::optional<ScriptedPlayerPose>&,
                  const formats::CustomizationDefinition&);
  MissionSimulation simulation_;
  StagePresentation presentation_;
  PlayerAnimationReview review_;
};
}  // namespace bw::game
