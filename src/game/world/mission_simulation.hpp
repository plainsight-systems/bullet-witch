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
#include "game/actors/alicia/combat/player_weapon_combat.hpp"
#include "game/actors/alicia/player_actor.hpp"
#include "game/actors/alicia/player_controller.hpp"
#include "game/actors/alicia/player_resources.hpp"
#include "game/actors/alicia/player_targeting.hpp"
#include "game/combat/melee_damage.hpp"
#include "game/combat/ordinary_hit_response.hpp"
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
                    const scene::StaticCollision&,
                    const formats::CustomizationDefinition&,
                    const std::optional<PlayerActorSource>&,
                    const std::shared_ptr<memory::Domain>&,
                    const content::LoadProgress&,
                    const std::optional<render::ViewPose>&,
                    const std::optional<ScriptedPlayerPose>&);
  bool advance(const world::WorldFrame&, StageTraversalInput,
               render::ActorPublisher&);
  const PlayerActor* actor() const { return player_ ? &*player_ : nullptr; }
  const PlayerTargeting& targeting_state() const { return targeting_; }
  // Copy the resource owner's emissions for this admitted frame. A terminal
  // empty status can still contain shots; combat consumes count independently.
  std::optional<ShotResourceResult> shot_receipt() const {
    return shot_receipt_;
  }
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
  // Read-only world scenery capability for source placement projection.
  // World owns immutable geometry; simulation cannot register or retire it.
  scene::SegmentClearanceResult clip_scenery(
      const scene::SegmentClearance& value) const {
    return collision_.clip_segment(value);
  }
  bool paused() const { return control_.simulation_paused(); }
  std::string_view action_notice() const { return action_notice_; }
  void synchronize_pause();
  void bind_action_resources(PlayerActionResources);
  // World owns the receiver and retires it before this simulation. Admission
  // precedes live input; only synchronous copied shot/recharge events escape.
  void bind_weapon_receiver(PlayerWeaponReceiver&);
  // Receiver owner revokes its borrow before destroying retained weapon state.
  void unbind_weapon_receiver(const PlayerWeaponReceiver&) noexcept;
  void bind_player_combat(PlayerActorCombatSource);
  // Live health remains mission-owned. This narrow transaction handle and
  // immutable response definition cannot reset progression or advance time.
  void bind_player_damage(PlayerDamageResources, const ActorDamageDefinition&,
                          SourceRandom&);
  bool receive_player_hit(const CombatImpact&);
  bool receive_player_melee(const MeleeImpact&);
  bool player_dead() const noexcept { return player_dead_; }
  // Copied source direction channels for this frame; same-channel hits
  // coalesce. No UI owner, callback lifetime, or mutation capability crosses
  // this boundary.
  std::uint8_t damage_direction_mask() const noexcept {
    return damage_direction_mask_;
  }
  void finish_physics(render::ActorPublisher&);
  PlayerWeaponPose weapon_pose() const;
  void bind_audio(const PreparedCharacterAudio&, audio::CueOutput&,
                  SourceRandom&, CharacterVoicePool&);
  void bind_sound_events(std::weak_ptr<ActorSoundEvents>);
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
  const scene::StaticCollision& collision_;
  PlayerTargeting targeting_;
  std::optional<PlayerActor> player_;
  std::optional<PlayerController> controller_;
  std::optional<PlayerCamera> camera_;
  InitialPlayerCamera fixed_view_;
  std::uint8_t damage_direction_mask_{};
  bool audio_paused_{};
  audio::CueOutput* audio_output_{};
  audio::Listener audio_listener_;
  bool automatic_targeting_camera_{};
  std::array<WeaponPresentationSettings, 4> weapon_settings_{};
  std::optional<PlayerActionResources> action_resources_;
  std::optional<ShotResourceResult> shot_receipt_;
  unsigned weapon_class_{};
  PlayerWeaponReceiver* weapon_receiver_{};
  std::optional<PlayerDamageResources> damage_resources_;
  const ActorDamageDefinition* damage_definition_{};
  SourceRandom* combat_random_{};
  bool player_dead_{};
  void present_input(const world::WorldFrame&, PlayerPresentationInput,
                     PlayerMovementInput);
  std::string_view action_notice_;
};
}  // namespace bw::game
