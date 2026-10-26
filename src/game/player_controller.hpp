// Ownership extraction: design I.1/C.9 and implementation E.6 (#re-raii)
// retain stable actor/controller borrows. Both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps existing
// prepared buffers; update-only publication cannot replace draws, lights or
// scene view. Actor publication borrows ActorPublisher only; it cannot upload,
// bind or retire mission resources. Both-phase C.121 (#rh-abstract), I.11
// (#ri-raw), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) enforce
// the existing owner split without changing frame work or lifetimes. Complete
// parent/model subdivisions follow the pinned Unreal balanced remaining-time
// algorithm; no fractional tail is manufactured by subtraction. Both design
// corpora: E.4 validates the timing invariant, MEM.9 keeps scheduling and CPU
// model completion in fixed storage. GPU publication is once per call. Shared
// raw-axis preprocessing returns source direction and separately capped
// magnitude together (Core F.21, #rf-out-multi); never recover the scalar from
// rounded normalized components. I.1/E.4 make this producer/consumer invariant
// explicit, and MEM.9 keeps the result allocation-free in both phases. Shared
// raw-axis preprocessing follows source scaling and radial rejection. Both MCP
// phases F.8 (#rf-pure) and MEM.9 retain a pure fixed-size value operation for
// locomotion and action admission, with no allocation. Grounded action
// selection uses the recovered stance policy. Consume one crouch press per
// admitted outer update before bounded movement substeps; StateSquat exit
// restores standing height before evade entry. Model clocks, upper layers and
// weapon clocks remain actor-owned. Both-phase I.1/E.4 and MEM.9 preserve
// explicit edge/state ownership with fixed update storage. Own Alicia's source
// hardware locomotion and directional evasive leap above prepared static
// contacts. Borrow actor/collision/targeting through this owner's lifetime;
// synchronous entry callbacks update posture and targeting before returning to
// action publication, without consuming time or publishing poses; a single game
// thread supplies raw normalized axes, held evade and camera yaw.
// Scale/deadzone input, select walking/dashing or directional evade, consume
// authored root displacement exactly once, resolve the lower-sphere-center
// body, then publish its corrected pose with the live negative-radius model
// offset. Evasive start/middle consume full XYZ root motion; only completed
// middle motion enters ballistic gravity. Landing uses double playback and its
// authored cancellation marker. Scripted generic jump is not the hardware evade
// action. Fixed state, prepared clips/geometry and bounded time subdivision
// allocate nothing per frame. Invalid input/settings throw. No mission Lua,
// combat, dynamic-body simulation or profile saving is implied. Design I.1
// (#ri-explicit), implementation E.4 (#re-design-invariants) expose validated
// action/state invariants. Both phases MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) retains fixed steady-state storage.
// See player-evasive-leap.md and player-collision-boundary.md.
#pragma once
#include "aim_posture.hpp"
#include "alicia_action_owner.hpp"
#include "bw/formats/customization.hpp"
#include "bw/render/render_scene.hpp"
#include "bw/scene/spatial/character_collision.hpp"
#include "player_actor.hpp"
#include "scripted_jump_policy.hpp"
namespace bw::game {
struct PlayerMovementInput {
  std::array<float, 2> direction{};
  bool evade_held{}, crouch_pressed{};
};
struct ProcessedPlayerMovement {
  std::array<float, 2> direction{};
  float magnitude{};  // Source scalar, exactly one after radial saturation.
};
// Apply source scale, radial deadzone and saturation, preserving both outputs.
// Finite raw axes produce a finite direction and magnitude in [0,1]; overflow
// throws. Normalized components may round outside the unit circle, so consumers
// must use magnitude rather than recompute it. No edge/state is consumed.
ProcessedPlayerMovement process_player_movement(std::array<float, 2> direction);
// Consume all remaining time when it fits the source float-domain 60 Hz bound;
// otherwise choose min(bound, remaining/2), balancing the final pair. Zero
// returns zero. Finite elapsed in [0,.25] is required. No time is discarded,
// rounded up or accumulated by this value-only scheduling operation.
double player_simulation_step(double remaining_seconds);
struct PlayerBodySettings {
  float radius{}, standing_height{}, alternate_height{};
  float evade_steering_multiplier{}, evade_steering_limit{};
  std::array<float, 4> aim_pitch_degrees{};
  static PlayerBodySettings parse(const formats::CustomizationDefinition& tree);
};
enum class PlayerMovementPhase : std::uint8_t {
  grounded,
  takeoff,
  airborne,
  landing
};
struct PlayerMovementState {
  ScriptedPlayerPose pose;
  // Full source model orientation; pose yaw is its camera-facing projection.
  animation::Matrix orientation{animation::kIdentityMatrix};
  std::array<float, 3> velocity{};
  scene::CharacterCollisionResult contact{};
  PlayerMovementPhase phase{PlayerMovementPhase::grounded};
  AliciaStance stance{AliciaStance::standing};
  bool weapon_raised{};
};
// Accepted aiming refresh is independent of a fired shot. The scene owns its
// current camera anchor; the input owner supplies the source-processed turn
// value and weapon policy supplies its movement scale/class. No settings are
// inferred from camera angular velocity or an uninitialized source global.
struct PlayerAimObservation {
  animation::Matrix anchor{animation::kIdentityMatrix};
  math::Vector3<float> retained_direction{0, 0, 1};
  float signed_turn_input{}, weapon_movement_scale{1};
  std::uint32_t weapon_class{};
  bool accepted_refresh{};
};
struct PlayerActionObservations {
  std::optional<float> random_unit;
  bool death_handoff_blocked{}, source_force_idle{},
      source_low_health_recovery{};
  SpellPresentationObservation spell{};
  PlayerAimObservation aim{};
};
class PlayerTargeting;
class PlayerController final : private ShooterEntrySink {
 public:
  PlayerController(PlayerActor& actor, const scene::StaticCollision& collision,
                   PlayerBodySettings settings, ScriptedPlayerPose placement,
                   PlayerTargeting& targeting);
  virtual ~PlayerController() = default;
  PlayerController(const PlayerController&) = delete;
  PlayerController& operator=(const PlayerController&) = delete;
  PlayerController(PlayerController&&) = delete;
  PlayerController& operator=(PlayerController&&) = delete;
  // Returns consumed simulation time; an unresolved action obligation stops
  // before the affected step, allowing its caller to retry without lost time.
  double advance(PlayerMovementInput input, float camera_yaw_radians,
                 double delta_seconds, render::ActorPublisher& output,
                 const PlayerActionObservations& observations = {});
  const PlayerMovementState& state() const { return state_; }
  // Source hardware Shooter handlers exist for ordinary stance and generic
  // jump start/flight, independently from the timed/posture-message handlers.
  bool accepts_shooter_input() const noexcept;
  // Accept an already authorized scripted request while grounded. Preflight
  // all reachable motion phases before replacing the current base owner;
  // unavailable content preserves it. Returned scope releases remain caller-
  // owned. This is separate from the hardware evade action.
  AliciaActionUpdate begin_scripted_jump(const AcceptedScriptedJump& request);

  // Accepted source messages may replace grounded base ownership. The caller
  // owns admission, scope acquisition/release and any required world service;
  // raw hardware presses do not imply acceptance. Airborne replacement needs
  // its separately reconstructed source policy. This owner survives costumes.
  AliciaActionOwner& action_owner() { return action_; }
  const AliciaActionOwner& action_owner() const { return action_; }
  const AliciaActionUpdate& action_status() const { return action_update_; }
  // Review-only physical snapshot restoration and paused class reset. Caller
  // supplies a snapshot from this controller; no teleport/mission semantics.
  // Clear exclusive/scripted/raised state without advancing actor clocks.
  // No allocation or failure; actor neutral publication is a separate step.
  void restore_review_state(const PlayerMovementState& snapshot) noexcept;
  void lower_review_weapon() noexcept;
  float body_radius() const noexcept { return settings_.radius; }

 private:
  bool step(float camera_yaw_radians, PlayerMovementInput input, double seconds,
            const PlayerActionObservations& observations);
  struct MovementStep {
    float camera_yaw_radians{};
    double seconds{};
  };
  bool step_scripted(MovementStep step, const PlayerAimObservation& aim);
  bool update_raised(const PlayerAimObservation& aim, float seconds);
  bool publishes_posture_motion() const noexcept;
  void force_shooter_posture(bool ordinary_camera) noexcept override;
  void update_aim_target(const PlayerAimObservation& aim);
  void publish_scripted_transition(const ScriptedJumpTransition& transition,
                                   float camera_yaw_radians);
  void enter_support_loss();
  bool script_motions_available(const ScriptedJumpPolicy& policy) const;
  PlayerActor* actor_;
  PlayerTargeting* targeting_;
  const scene::StaticCollision* collision_;
  PlayerBodySettings settings_;
  PlayerMovementState state_;
  GroundIdlePolicy idle_;
  RaisedWeaponLifetime raised_;
  AliciaActionOwner action_;
  AliciaActionUpdate action_update_;
  ScriptedJumpPolicy scripted_jump_;
  math::Vector3<float> scripted_camera_direction_{0, 0, 1};
  bool had_exclusive_action_{};
  std::uint8_t evade_direction_{};
  float evade_facing_{};
  std::array<float, 2> evade_steering_{};
  bool evading_{}, ballistic_{};
};
}  // namespace bw::game
