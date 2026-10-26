// Debug separation: Core I.1 (#ri-explicit), R.3 (#rr-ptr), and performance
// TLM.1 (guidelines/telemetry/TLM.1-compile-telemetry-out-by-default.md),
// retrieved in design and implementation, keep preview capabilities bounded
// and compile out their owner/state/calls in ordinary production builds.
// Ownership split of the existing stage runtime; no new simulation algorithm.
// Design Core I.1 (#ri-explicit), C.9 (#rc-private) and R.21 (#rr-unique)
// keep authority private and borrows explicit. Implementation E.6 (#re-raii)
// preserves destruction/unwind order. Both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) reserves domain
// buffers during preparation; frame operations retain their bounded fixed
// footprint. COPY.4 (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md)
// keeps stable owner identities noncopyable/nonmovable. See
// docs/architecture/reorganization-plan.md. Own mission contact,
// player/controller/camera and accepted action admission. Clock/control are
// const borrows. Reject foreign/replayed frames before work; synchronize audio
// pause, step parent/model pairs, then complete the camera. The caller samples
// environment and supplies only its far-distance result. Actor publication
// receives update-only authority; appearance replacement uses
// StagePresentation's transaction and commits residency only after publication.
// Physical state is const-only; the existing action-owner admission capability
// remains explicit. No mutable actor/collision borrow escapes. Single game
// thread.
#pragma once
#include "game/actors/alicia/player_actor.hpp"
#include "game/actors/alicia/player_controller.hpp"
#include "game/actors/alicia/player_resources.hpp"
#include "game/actors/alicia/player_targeting.hpp"
#include "game/mission/mission_control.hpp"
#include "game/player/camera/player_camera.hpp"
#include "stage_source.hpp"
namespace bw::game {
class MissionLoading;
class StagePresentation;
class AnimationPreview;
class AnimationPreviewAccess;
class WorldRuntime;
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
class MissionSimulation {
 public:
  ~MissionSimulation() = default;
  MissionSimulation(const world::WorldClock&, const MissionControl&,
                    const content::StageDefinition&,
                    const formats::CustomizationDefinition&,
                    StageBackgroundSource,
                    const std::optional<PlayerActorSource>&,
                    const std::shared_ptr<memory::Domain>&,
                    const content::LoadProgress&,
                    const std::optional<render::ViewPose>&,
                    const std::optional<ScriptedPlayerPose>&);
  bool advance(const world::WorldFrame&, StageTraversalInput,
               render::ActorPublisher&);
  const PlayerActor* actor() const { return player_ ? &*player_ : nullptr; }
  const PlayerTargeting& targeting_state() const { return targeting_; }
  const PlayerMovementState* player_state() const {
    return controller_ ? &controller_->state() : nullptr;
  }
  AliciaActionOwner* player_actions() {
    return controller_ ? &controller_->action_owner() : nullptr;
  }
  AliciaActionUpdate begin_player_scripted_jump(const AcceptedScriptedJump&);
  const AliciaActionUpdate* player_action_status() const {
    return controller_ ? &controller_->action_status() : nullptr;
  }
  const InitialPlayerCamera* camera_state() const {
    return camera_ ? &camera_->view() : nullptr;
  }
  // Initial environment/draw admission consumes the authored snapped snapshot;
  // completed live camera publication is a distinct source phase.
  const InitialPlayerCamera& initial_camera() const { return fixed_view_; }
  const InitialPlayerCamera& presentation_camera() const {
    return camera_ ? camera_->view() : fixed_view_;
  }
  bool player_forces_visibility() const {
    return controller_ && controller_->action_owner().forces_model_visibility();
  }
  void set_camera_far(float distance);
  bool paused() const { return control_.simulation_paused(); }
  std::string_view action_notice() const { return action_notice_; }
  void synchronize_pause();
  void bind_action_resources(PlayerActionResources);
  void bind_audio(const PreparedCharacterAudio&, audio::CueOutput&,
                  SourceRandom&);
  void change_costume(MissionLoading&, unsigned, StagePresentation&);
  void replace_weapon(MissionLoading&, unsigned, bool animate_switch,
                      StagePresentation&);
  MissionSimulation(const MissionSimulation&) = delete;
  MissionSimulation& operator=(const MissionSimulation&) = delete;
  MissionSimulation(MissionSimulation&&) = delete;
  MissionSimulation& operator=(MissionSimulation&&) = delete;

 private:
#if BW_DEVELOPER_VIEWS
  friend class AnimationPreviewAccess;
  friend class WorldRuntime;
  AnimationPreview* preview_{};  // World owns; destroyed before simulation.
#endif
  const world::WorldClock& clock_;
  const MissionControl& control_;
  std::uint64_t last_frame_{};
  std::shared_ptr<memory::Domain> memory_;
  scene::StaticCollision collision_;
  PlayerTargeting targeting_;
  std::optional<PlayerActor> player_;
  std::optional<PlayerController> controller_;
  std::optional<PlayerCamera> camera_;
  InitialPlayerCamera fixed_view_;
  bool audio_paused_{};
  audio::CueOutput* audio_output_{};
  audio::Listener audio_listener_;
  bool automatic_targeting_camera_{};
  std::array<WeaponPresentationSettings, 4> weapon_settings_{};
  std::optional<PlayerActionResources> action_resources_;
  unsigned weapon_class_{};
  void present_input(PlayerPresentationInput, PlayerMovementInput);
  std::string_view action_notice_;
};
}  // namespace bw::game
