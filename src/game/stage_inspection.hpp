// Engine timing integration: borrow the mission control and world-clock owners
// through teardown as const observations; it cannot mutate either owner.
// Core Con.2 (#rconst-fct) enforces that restriction in both phases. This
// consumer accepts engine-issued frames, rejects foreign or replayed frames,
// and samples material time from the admitted world total. It owns neither a
// master clock nor a pause flag. Audio pause is a cached device observation of
// mission control. Input/UI routing precedes publication; local actor
// subdivisions retain their existing complete parent/model order. Design Core
// C.9/I.1; implementation E.4 (#re-design-invariants); both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps frame state
// fixed and allocation-free. See docs/architecture/world-timing.md. Own
// retained scene presentation and bounded Alicia traversal over mission assets.
// Borrow definitions/device identities until destruction; the mission outlives
// this owner. Build authored static and placed-prop collision, prepare
// character queries, decode body/camera settings and construct actor/controller
// once. Each frame routes explicit input through custom movement/contact, live
// camera and pose publication, then updates existing view/material constants.
// Pause withholds simulation, animation and camera time. A diagnostic camera
// override keeps the independent presentation path; neither path completes Lua
// mission start. Clear submitted draws before retiring borrowed asset owners.
// Setup owns domain-backed collision/pose/curve buffers. Steady state performs
// prepared spatial queries, O(bones + palette references) pose work and unique
// curve samples; no geometry upload, sampler rebuild or draw-list replacement.
// Both MCP phases: I.1 (#ri-explicit) separates input and publication
// contracts; E.4 (#re-design-invariants) validates before publishing values;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) reserves runtime
// storage; GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md) retains device
// resources. Existing material preparation also uses P.9 (#rp-waste) to sample
// unique curves once. See actors-and-animation.md,
// player-collision-boundary.md, player-camera-follow.md and
// stage-scene-presentation.md.
#pragma once
#include <functional>
#include <optional>

#include "animation_review.hpp"
#include "bw/content/stage.hpp"
#include "bw/formats/background_placement.hpp"
#include "bw/render/material_animation.hpp"
#include "bw/render/view.hpp"
#include "bw/render/world.hpp"
#include "bw/scene/lighting/scene_lighting.hpp"
#include "camera/player_camera.hpp"
#include "game/mission_control.hpp"
#include "player_actor.hpp"
#include "player_controller.hpp"
#include "player_targeting.hpp"
#include "resident_model.hpp"
#include "stage_environment.hpp"
namespace bw::game {
class MissionLoading;
// Definitions and model handles have identical order. Their mission owner must
// outlive the view; the renderer validates each handle at submission.
struct StageViewSource {
  std::reference_wrapper<const content::StageDefinition> definition;
  std::span<const render::WorldModelHandle> models;
};
// Optional placed-prop snapshot; all borrowed inputs are consumed during view
// construction. This does not execute mission start or advance physics.
struct StageBackgroundSource {
  const formats::BackgroundPlacementDefinition* placements{};
  std::span<const ResidentModelView> models;
};
// Original mission light publications, borrowed only during construction.
// Alicia requires character data; enemy is retained as an explicit separate
// source capability for future actor admission, not sampled for Alicia.
struct StageCharacterLightSource {
  const content::UserDefinition* character{};
  const content::UserDefinition* enemy{};
};
struct PlayerPresentationInput {
  float fire_trigger{};
  bool reload_pressed{}, attack_pressed{}, aim_mode_pressed{};
};
struct StageTraversalInput {
  PlayerMovementInput movement;
  PlayerCameraLook look;

  PlayerActionObservations action{};
  PlayerPresentationInput presentation{};
};
class StageInspection {
 public:
  // Playerless inspection requires an explicit placement; no mission default
  // is hidden in this reusable view. Player input owns its own placement.
  StageInspection(
      const world::WorldClock& clock, const MissionControl& control,
      render::WorldOutput& output, StageViewSource stage, StageViewSource sky,
      const std::shared_ptr<memory::Domain>& domain,
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
  const PlayerTargeting& targeting_state() const { return targeting_; }
  const PlayerMovementState* player_state() const {
    return controller_ ? &controller_->state() : nullptr;
  }
  // Accepted game messages reach the existing player lifetime. This is not
  // hardware/combat admission; callers fulfill reported scope/handoff needs.
  // Empty for a presentation-only scene without a physical player controller.
  AliciaActionOwner* player_actions() {
    return controller_ ? &controller_->action_owner() : nullptr;
  }
  AliciaActionUpdate begin_player_scripted_jump(
      const AcceptedScriptedJump& request);
  const AliciaActionUpdate* player_action_status() const {
    return controller_ ? &controller_->action_status() : nullptr;
  }
  const InitialPlayerCamera* camera_state() const {
    return camera_ ? &camera_->view() : nullptr;
  }
  // Borrow completed unskinned world transforms until the next advance or
  // scene destruction. Empty without a player. No copies, allocations or pose
  // evaluation; same owner-thread contract as player_state. I.1/MEM.9.
  std::span<const animation::Matrix> player_world_pose() const {
    return player_ ? player_->body_bones()
                   : std::span<const animation::Matrix>{};
  }
  // Borrow the selected prepared bank clock until the next action/update or
  // appearance replacement. Diagnostic observation never advances playback.
  const animation::ClipPlaybackState* player_animation_state(
      CharacterMotionBank bank) const {
    return player_ ? player_->animation_state(bank) : nullptr;
  }
  bool paused() const { return control_.simulation_paused(); }
  // Publish pause to the audio device before any paused resource transaction.
  void synchronize_pause();
  // Mission-owned output/prepared policy and session RNG outlive this view.
  // Bind once after actor admission; pause and listener updates follow the
  // same stage clock as its animation event owners.
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
    if (!player_) return nullptr;
    return weapon ? &player_->weapon_animations().motions()
                  : &player_->body_animations().motions();
  }
  // Same lifetime and indices as review_catalog(); no copied classification.
  std::span<const MotionPoseSemantics> review_pose_semantics(
      bool weapon) const {
    if (!player_) return {};
    return weapon ? player_->weapon_animations().pose_semantics()
                  : player_->body_animations().pose_semantics();
  }
  AnimationReviewResult review(AnimationReviewRequest request);
  std::string_view review_status() const { return review_message_.data(); }
  StageInspection(const StageInspection&) = delete;
  StageInspection& operator=(const StageInspection&) = delete;
  StageInspection(StageInspection&&) = delete;
  StageInspection& operator=(StageInspection&&) = delete;
  std::size_t selected_draws() const { return selected_draws_; }
  std::size_t omitted_draws() const { return omitted_draws_; }
  std::size_t sky_draws() const { return sky_draws_; }
  std::size_t background_draws() const { return background_draws_; }
  std::size_t player_draws() const { return player_draws_; }

 private:
  const world::WorldClock& clock_;
  const MissionControl& control_;
  std::uint64_t last_frame_{};
  render::WorldOutput& output_;
  std::shared_ptr<memory::Domain> memory_;
  void append_player_draws(memory::Vector<render::WorldColorDraw>& draws,
                           const PlayerActor& actor, bool weapon,
                           const InitialPlayerCamera& camera);
  scene::StaticCollision collision_;
  // Scene owns fields and emitter selection; game maps authored field slots
  // and supplies model observations. Controllers never own lighting state.
  std::optional<scene::SceneLighting> lighting_;
  scene::LightingFieldId character_field_, enemy_field_;
  memory::Vector<render::WorldLightingUpdate> static_lighting_updates_;
  render::WorldLightingUpdate model_lighting(
      const ResidentModelView& resident, const animation::Matrix& transform);
  void publish_player_lighting();
  PlayerTargeting targeting_;
  std::optional<PlayerActor> player_;
  std::optional<PlayerController> controller_;
  std::optional<PlayerCamera> camera_;
  std::optional<formats::StageEnvironment> environment_;
  std::optional<scene::Environment> environment_channels_;
  std::optional<InitialPlayerCamera> fixed_view_;
  bool audio_paused_{};
  audio::CueOutput* audio_output_{};
  audio::Listener audio_listener_;
  void publish_view();
  void stop_review();
  void reset_review_presentation(const PlayerMovementState& state);
  void replace_weapon(MissionLoading& loading, unsigned weapon_class,
                      bool animate_switch);
  void set_review_status(std::string_view text, std::string_view detail = {});
  void prepare_review_observations(PlayerActionObservations& observation);
  void present_input(PlayerPresentationInput input,
                     PlayerMovementInput movement, double delta_seconds);
  std::optional<PlayerMovementState> review_entry_;
  bool review_fixed_pose_{}, review_next_phase_{}, review_thunder_striking_{};
  bool review_scripted_posture_{};
  bool automatic_targeting_camera_{};
  std::optional<SpellPresentationKind> review_spell_;
  static constexpr std::size_t kReviewMessageCapacity = 160;
  std::array<char, kReviewMessageCapacity> review_message_{};
  std::array<WeaponPresentationSettings, 4> weapon_settings_{};
  std::optional<WeaponPresentationGate> weapon_gate_;
  std::array<std::optional<WeaponPresentationGate>, 4> inactive_weapon_gates_;
  unsigned weapon_class_{};
  AcceptedDeathPresentation review_death_;
  struct AnimatedMaterial {
    std::array<std::optional<std::size_t>, 2> curves;
  };
  struct AnimatedCurve {
    std::string_view name;
    render::MaterialFloat2Curve curve;
    std::array<float, 2> value;
  };
  memory::Vector<AnimatedCurve> animated_curves_;
  memory::Vector<AnimatedMaterial> animated_materials_;
  memory::Vector<render::WorldTextureOffsetUpdate> texture_updates_;

  std::size_t selected_draws_{}, omitted_draws_{}, sky_draws_{},
      background_draws_{}, player_draws_{};
};
}  // namespace bw::game
