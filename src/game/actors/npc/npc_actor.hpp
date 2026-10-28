// Source-initialized NPC presentation instances. Mission preparation owns the
// immutable model, selected draw policy and decoded initial motion; each actor
// borrows them while owning its playback, local pose and skin palettes. The
// world alone supplies elapsed simulation time and placement. This is the
// original grounded Runner initialization, not a behavior-tree simulation.
// Prepare palettes once, advance the selected original controller, suppress
// its consumed root, compose the model and completed hand attachment, then
// publish only this instance identity. No archive reads/uploads or allocations
// during advance. Failure during construction publishes nothing. Core I.11
// (#ri-raw), E.6 (#re-raii), and performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), read for both phases,
// preserve immutable shared inputs and distinct mutable instance state.
#pragma once
#include "bw/animation/playback_ring.hpp"
#include "bw/content/formats/control_points.hpp"
#include "bw/render/scene/render_scene.hpp"
#include "bw/spatial/static_collision.hpp"
#include "game/actors/animation/action_motion_plan.hpp"
#include "game/actors/animation/aim_posture.hpp"
#include "game/actors/animation/character_motion_player.hpp"
#include "game/actors/animation/character_reaction_policy.hpp"
#include "game/actors/animation/facial_animation.hpp"
#include "game/actors/animation/melee_blast.hpp"
#include "game/actors/animation/model_visibility.hpp"
#include "game/actors/animation/scripted_jump_policy.hpp"
#include "game/actors/character_physics.hpp"
#include "game/actors/death_lifetime.hpp"
#include "game/combat/actor_hit_geometry.hpp"
#include "game/combat/combat_world.hpp"
#include "game/combat/melee_damage.hpp"
#include "game/combat/ordinary_hit_response.hpp"
#include "game/world/mission_population.hpp"
#include "locomotion_policy.hpp"
#include "npc_appearance.hpp"
#include "npc_initial_motion.hpp"
#include "npc_motion_events.hpp"
namespace bw::game {
// Immutable source shapes and expression keys shared by resident appearance.
// Instance poses below borrow this stable owner and retain only output/clock
// state.
struct PreparedNpcShapes {
  PreparedNpcShapes(const formats::ModelDefinition&,
                    const content::CharacterSupport&,
                    std::span<const std::string_view>,
                    const std::shared_ptr<memory::Domain>&);
  PreparedNpcShapes(const PreparedNpcShapes&) = delete;
  PreparedNpcShapes& operator=(const PreparedNpcShapes&) = delete;
  PreparedNpcShapes(PreparedNpcShapes&&) = delete;
  PreparedNpcShapes& operator=(PreparedNpcShapes&&) = delete;
  ~PreparedNpcShapes() = default;
  animation::PreparedModelShapes model;
  PreparedFacialExpressions expressions;
};
struct PreparedNpcModel {
  ResidentModelView resident;
  NpcInitialMotion motion;
  PreparedNpcAppearance appearance;
  std::shared_ptr<const CharacterAnimations> animations;
  std::shared_ptr<const PreparedNpcShapes> shapes{};
  // Immutable model-local attachment metadata shared by every instance.
  std::shared_ptr<const formats::ControlPointDefinition> attachments{};
};
class NpcModelPose {
 public:
  NpcModelPose(const PreparedNpcModel&, const std::shared_ptr<memory::Domain>&);
  NpcModelPose(const NpcModelPose&) = delete;
  NpcModelPose& operator=(const NpcModelPose&) = delete;
  NpcModelPose(NpcModelPose&&) = delete;
  NpcModelPose& operator=(NpcModelPose&&) = delete;
  ~NpcModelPose() = default;
  void advance(double seconds, const animation::Matrix& transform);
  // Split clock/root consumption from pose publication for grounded actors.
  // Exactly one evaluate_pose follows each advance_motion; neither owns time.
  void advance_motion(double seconds);
  void evaluate_pose(double seconds, const animation::Matrix& transform);
  math::Vector3<float> root_displacement() const;
  animation::Matrix root_rotation() const;
  float horizontal_root_distance() const;
  animation::Matrix authored_facing_reference() const;
  bool upper_active() const;
  void enable_source_modifiers();
  void aim(math::Vector3<float> component_direction, bool active);
  bool has_motion(std::uint32_t slot) const;

  // Select source catalog slots by their prepared representation. Ordinary and
  // additive owners retain separate clocks; neither advances outside advance.
  bool request_motion(std::uint32_t slot, bool fresh = false,
                      double blend_seconds = kSourceActionBlendSeconds);
  bool request_weapon_motion(std::string_view name, bool fresh = false);
  // Source plans select explicit banks. Validate representation before a bank
  // changes; unbound slots return false. Shared resolution preserves authored
  // frame starts, channels and phase without a second action clock.
  bool request_action(const ActionMotionRequest&);
  void clear_upper();
  const animation::ClipPlaybackState* current_motion() const;
  bool cancellable() const;
  std::span<const NpcMotionEvent> events(std::size_t bank = 0) const {
    return events_.at(bank).pending();
  }
  void consume_events() noexcept {
    for (auto& bank : events_) bank.consume();
  }
  FacialAdmission facial(FacialMotionCommand) noexcept;
  void reset_facial() noexcept;
  void visibility(std::size_t bank, VisibilityMotionCommand command);
  void reset_visibility(std::size_t bank);
  void set_visible_group(std::uint32_t group, bool enabled) noexcept;
  bool draw_visible(std::size_t draw) const;
  void publish(std::uint64_t instance, render::ActorPublisher&) const;
  const animation::SkeletonPose& pose() const { return pose_; }
  animation::Matrix publish_physics(CharacterPhysics& physics);
  const PreparedNpcModel& prepared() const { return prepared_; }
  std::span<const render::WorldJointMatrix> palette(std::size_t draw) const;
  std::span<const animation::Matrix> bones() const {
    return pose_.bone_transforms();
  }

 private:
  static constexpr std::size_t kPaletteCapacity = 32;
  struct Palette {
    std::array<std::size_t, kPaletteCapacity> bones{};
    std::array<render::WorldJointMatrix, kPaletteCapacity> matrices{};
    std::size_t draw{}, count{};
  };
  const PreparedNpcModel& prepared_;
  animation::PlaybackRing playback_;
  static constexpr std::size_t kPresentationBanks = 3;
  std::array<NpcMotionEvents, kPresentationBanks> events_;
  std::optional<CharacterMotionPlayer> ordinary_, upper_, additive_;
  static constexpr std::size_t kWeaponSlots = 7;
  std::array<std::optional<std::size_t>, kWeaponSlots> weapon_slots_{};
  animation::SkeletonPose pose_;
  animation::PoseReleaseState release_;
  memory::Vector<formats::SkeletonTransform> locals_;
  memory::Vector<Palette> palettes_;
  ModelVisibility visibility_;
  render::WorldJointMatrix model_transform_{};
  std::size_t root_{};
  animation::Matrix pending_root_{animation::kIdentityMatrix};
  animation::BoneLocalBasisOverride direction_reference_{};
  static constexpr std::size_t kAimChainCapacity = 6;
  std::array<std::size_t, kAimChainCapacity> aim_chain_{};
  std::size_t aim_chain_count_{};
  AimPostureBlend aim_blend_;
  math::Vector3<float> aim_direction_{0, 0, 1};
  bool aim_active_{}, source_modifiers_{};

  struct ShapeState;
  std::shared_ptr<ShapeState> shapes_;
  void update_palettes();
};
struct NpcCombatLayers {
  std::uint8_t hit{}, body{};
};
class NpcActor {
 public:
  NpcActor(PopulationIdentity, PopulationActorState,
           const PreparedNpcModel& body, const PreparedNpcModel* weapon,
           float body_radius, const std::shared_ptr<memory::Domain>&);
  NpcActor(const NpcActor&) = delete;
  NpcActor& operator=(const NpcActor&) = delete;
  NpcActor(NpcActor&&) = delete;
  NpcActor& operator=(NpcActor&&) = delete;
  ~NpcActor() = default;
  void advance(double seconds, render::ActorPublisher&);
  // World admission supplies weak capabilities and shared source geometry.
  // The actor owns only its receiver/body lifetime, never the physics step.
  void bind_combat(const physics::Resources&, const physics::Queries&,
                   const physics::Mutations&,
                   std::shared_ptr<const ActorHitGeometry>,
                   const CharacterPhysicsRecipe&, physics::Material,
                   NpcCombatLayers, const std::shared_ptr<memory::Domain>&);
  void finish_physics(render::ActorPublisher&);
  ActionPresentationAdmission apply_action_plan(const ActionMotionPlan&);
  // Copied impacts enter the actor's actual health and source response owner.
  // The selected grounded soldier has classification2: it does not use the
  // civilian/player low-health recovery state. Native reaction policies and
  // existing motion banks own pose selection; death transfers to the admitted
  // skeletal provider only at its authored completion/support boundary.
  HitReception receive_ordinary_hit(const CombatImpact&,
                                    const ActorDamageDefinition&,
                                    SourceRandom&);
  bool receive_melee(const MeleeImpact&, const ActorDamageDefinition&,
                     SourceRandom&);
  void accepted_shot();
  void set_hit_query_flags(std::uint32_t mask, bool enabled);
  // Native AI contact states own ordinary motion until completion or an
  // interrupting damage/death owner. Missing optional native slot retains
  // the previous playback controller; admission remains observable.
  bool begin_contact_action(const ActionMotionRequest&);
  void end_contact_action() noexcept;
  bool contact_action_active() const noexcept { return contact_action_; }
  void bind_locomotion(const scene::StaticCollision&, float radius,
                       float height);
  void locomotion(const NpcLocomotionSelection&,
                  const animation::Matrix& aim_anchor, bool weapon_ready,
                  float seconds);
  void face(const math::Vector3<float>& direction);
  void aim(const animation::Matrix& anchor, bool active);
  math::Vector3<float> forward() const;
  math::Vector3<float> velocity() const { return velocity_; }
  // Original target point uses completed eye height, with resident group radius
  // fallback. This is a copied observation, independent of collision
  // dimensions.
  math::Vector3<float> aim_position() const;
  math::Affine3<float> attachment_transform(std::int32_t slot) const;
  math::Vector3<float> attachment_position(std::int32_t slot) const;
  // Motion sound/contact events admit positive locator slots only.
  math::Vector3<float> motion_attachment_position(std::int32_t slot) const;
  FacialAdmission facial(FacialMotionCommand command) noexcept {
    return body_.facial(command);
  }
  void reset_facial() noexcept { body_.reset_facial(); }
  void visibility(std::size_t bank, VisibilityMotionCommand command);
  void reset_visibility(std::size_t bank);
  bool has_reaction(std::uint32_t slot) const;
  bool react(std::uint32_t slot, double blend_seconds, bool update_facing);
  void reaction_facing(const math::Vector3<float>& retained_heading,
                       const animation::Matrix& anchor, bool ready,
                       float seconds);
  void clear_reaction();
  bool action_reaction_active() const { return action_reacting_; }
  bool motion_looping() const;
  bool motion_completed() const;
  std::span<const NpcMotionEvent> motion_events(std::size_t bank = 0) const {
    return body_.events(bank);
  }
  void consume_motion_events() noexcept { body_.consume_events(); }

  bool damage_reaction_active() const noexcept { return reacting_; }
  bool dying() const noexcept { return dying_; }
  bool melee_blast_active() const noexcept { return melee_blast_.active(); }
  bool physical() const noexcept { return physical_ && physical_->physical(); }
  // Death time advances with this actor, including physical ownership. The
  // world supplies copied camera/classification values and alone retires it.
  // Core I.11/MEM.9: no destruction capability or extra clock escapes.
  bool corpse_retirement_due(const DeathRetirementObservation&) const;

  PopulationIdentity identity() const { return identity_; }
  const PopulationActorState& state() const { return state_; }
  const NpcModelPose& body() const { return body_; }
  const NpcModelPose* weapon() const { return weapon_ ? &*weapon_ : nullptr; }
  const animation::Matrix& model_transform(bool weapon) const {
    return weapon ? weapon_transform_ : body_transform_;
  }

 private:
  void update_facing(const math::Vector3<float>& retained_heading,
                     const animation::Matrix& anchor, bool ready,
                     float seconds);
  bool reaction_updates_facing_{};
  std::uint8_t movement_category() const;
  void handoff_death();
  void evaluate(double seconds);
  void advance_reaction(double seconds);
  void require_plan(const ActionMotionPlan&);
  void apply_flight_transition(const ScriptedJumpTransition&);
  PopulationIdentity identity_;
  PopulationActorState state_;
  NpcModelPose body_;
  std::optional<NpcModelPose> weapon_;
  std::optional<std::size_t> attachment_;
  std::uint32_t weapon_rest_slot_{1};
  // Physical orientation survives authored root rotation; Shooter anchor is a
  // separate owner. Placement yaw remains a copied external observation.
  animation::Matrix orientation_{animation::kIdentityMatrix};
  animation::Matrix body_transform_, weapon_transform_;
  std::optional<ActorHitInstance> hit_;
  std::optional<CharacterPhysics> physical_;
  DamageReaction damage_;
  MeleeBlast melee_blast_;
  ScriptedJumpPolicy flight_;
  math::Vector3<float> flight_velocity_{};
  bool blast_handoff_{};
  SustainedDamageReaction sustained_;
  SourceRandom*
      combat_random_{};  // Session borrow, only after combat admission.
  bool reacting_{}, dying_{};
  DeathLifetime death_lifetime_;
  const scene::StaticCollision* collision_{};
  float radius_{}, height_{}, model_radius_{};
  // Common actor publication owns the source model origin. Physics publishes
  // model root; actor position is root minus this retained offset (602b70,
  // 683d40). Design I.1/E.4 and MEM.9: one scalar, no second pose authority.
  float model_offset_y_{};
  scene::CharacterCollisionResult contact_;
  math::Vector3<float> velocity_{}, locomotion_direction_{};
  bool action_reacting_{}, contact_action_{};
};
}  // namespace bw::game
