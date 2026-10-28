// Script-world forwarding follows implementation Core I.4/Con.2 and MEM.9:
// copy completed observations, forward only visibility, retain no second state.
// Debug separation: Core I.1 (#ri-explicit), R.3 (#rr-ptr), and performance
// TLM.1 (guidelines/telemetry/TLM.1-compile-telemetry-out-by-default.md),
// retrieved in design and implementation, keep preview capabilities bounded
// and compile out their owner/state/calls in ordinary production builds.
// Implement the owner contract in the matching header. Design I.1/C.9 keeps
// authoritative state private; implementation E.6 (#re-raii) preserves cleanup
// on failed preparation. Both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps retained buffers
// fixed during admitted frames. Existing source algorithms and ordering remain.
#include "world_runtime.hpp"

#include "game/mission/loading/mission_loading.hpp"
#include "placed_collision.hpp"
#include "world_collision.hpp"
#include "world_combat.hpp"
#include "world_physics.hpp"
#include "world_population.hpp"
namespace bw::game {
ScriptWorldControl::ScriptWorldControl(WorldRuntime& owner) noexcept
    : owner_(&owner) {}
std::optional<ScriptedPlayerPose> ScriptWorldControl::player_pose() const {
  const auto* player = owner_->player_state();
  return player ? std::optional{player->pose} : std::nullopt;
}
PopulationCheckpoint ScriptWorldControl::population_checkpoint() const {
  return owner_->population_checkpoint();
}
void ScriptWorldControl::set_player_visible(bool visible) {
  owner_->set_player_visible(visible);
}
void ScriptWorldControl::remove_population(PopulationFamily family) {
  owner_->remove_population(family);
}
void ScriptWorldControl::remove_background() { owner_->remove_background(); }
void WorldRuntime::remove_background() {
  if (combat_) combat_->retire_background();
  presentation_.retire_background();
  collision_.set_group_enabled(placed_background_collision_group, false);
}
void WorldRuntime::remove_population(PopulationFamily family) {
  if (!population_) {
    throw std::logic_error("Population removal requires admitted world");
  }
  population_->remove_family(family);
}
ScriptWorldControl WorldRuntime::script_world_control() noexcept {
  return ScriptWorldControl{*this};
}
WorldRuntime::WorldRuntime(
    const world::WorldClock& clock, const MissionControl& control,
    render::WorldPresentation& output, StageViewSource stage,
    StageViewSource sky, const std::shared_ptr<memory::Domain>& domain,
    const std::optional<render::ViewPose>& camera_pose,
    StageBackgroundSource background,
    const std::optional<PlayerActorSource>& player,
    const content::LoadProgress& progress, StageCharacterLightSource light,
    const std::optional<ScriptedPlayerPose>& inspection_placement)
    : WorldRuntime(clock, control, output, stage, sky, domain, camera_pose,
                   background, player, progress, light, inspection_placement,
                   formats::CustomizationDefinition::parse(
                       stage.definition.get().customization_bytes(), domain)) {}
WorldRuntime::WorldRuntime(
    const world::WorldClock& clock, const MissionControl& control,
    render::WorldPresentation& output, StageViewSource stage,
    StageViewSource sky, const std::shared_ptr<memory::Domain>& domain,
    const std::optional<render::ViewPose>& camera_pose,
    StageBackgroundSource background,
    const std::optional<PlayerActorSource>& player,
    const content::LoadProgress& progress, StageCharacterLightSource light,
    const std::optional<ScriptedPlayerPose>& inspection_placement,
    const formats::CustomizationDefinition& customization)
    : clock_(clock),
      collision_(
          prepare_world_collision(stage.definition.get(), background, domain)),
      simulation_(clock, control, collision_, customization, player, domain,
                  progress, camera_pose, inspection_placement),
      presentation_(output, stage, sky, background, light, customization,
                    simulation_.actor(),
                    {.initial = simulation_.initial_camera(),
                     .completed = simulation_.presentation_camera()},
                    simulation_.player_forces_visibility(), domain),
      memory_(domain) {
#if BW_DEVELOPER_VIEWS
  review_ = make_animation_preview(
      AnimationPreviewAccess(simulation_, presentation_.actor_publisher()),
      customization, domain);
  simulation_.preview_ = review_.get();
#endif
  simulation_.set_camera_far(presentation_.initial_far_distance());
}
WorldRuntime::~WorldRuntime() = default;
void WorldRuntime::advance(const world::WorldFrame& frame,
                           StageTraversalInput input) {
  if (!frame.belongs_to(clock_) || frame.sequence() <= last_frame_) {
    throw std::invalid_argument("Foreign or repeated world runtime frame");
  }
  last_frame_ = frame.sequence();
  if (frame.admission().simulation) {
    if (combat_) combat_->begin_frame(frame);
    const auto* player = simulation_.player_state();
    const auto position =
        player ? player->pose.position : math::Vector3<float>{};
    const auto voice_result =
        voices_ ? voices_->advance(frame.delta_seconds(), position)
                : CharacterAudioResult::suppressed;
    if (voice_result == CharacterAudioResult::backend_failure ||
        voice_result == CharacterAudioResult::capacity) {
      throw std::runtime_error("World character voice update failed");
    }
  }
  if (combat_) combat_->prepare_player_presentation();
  if (!simulation_.advance(frame, input, presentation_.actor_publisher())) {
    return;
  }
  if (population_) population_->update_population();
  if (combat_) combat_->advance_actors(frame);
  if (population_) population_->advance_actors(frame.delta_seconds());
  if (physics_) physics_->advance(frame);
  simulation_.finish_physics(presentation_.actor_publisher());
  if (population_) population_->finish_physics();
  if (physics_) physics_->publish_queries(frame);
  if (combat_) combat_->advance(frame);
  if (effects_) effects_->advance(frame, {});
  if (const auto far =
          presentation_.update_environment(simulation_.presentation_camera())) {
    simulation_.set_camera_far(*far);
  }
  presentation_.present(simulation_.presentation_camera(),
                        simulation_.player_forces_visibility(),
                        frame.simulation_seconds());
  if (effects_) {
    effects_->publish(
        presentation_.effect_view(simulation_.presentation_camera()));
  }
}
AliciaActionUpdate WorldRuntime::begin_player_scripted_jump(
    const AcceptedScriptedJump& request) {
  return simulation_.begin_player_scripted_jump(request);
}
void WorldRuntime::synchronize_pause() { simulation_.synchronize_pause(); }
void WorldRuntime::bind_action_resources(PlayerActionResources resources) {
  simulation_.bind_action_resources(resources);
}
std::size_t WorldRuntime::observe_population(
    std::span<PopulationActorObservation> output) const {
  if (!population_) throw std::logic_error("Mission population is not bound");
  return population_->observe_actors(output);
}
PopulationCheckpoint WorldRuntime::population_checkpoint() const {
  if (!population_) throw std::logic_error("Mission population is not bound");
  return population_->checkpoint();
}
void WorldRuntime::bind_population(const MissionLoading& loading,
                                   PlacementRandom& random) {
  if (population_) {
    throw std::logic_error("Mission population is already bound");
  }
  if (!physics_) {
    throw std::logic_error("Mission population needs world physics");
  }
  population_ = std::allocate_shared<WorldPopulation>(
      memory::Allocator<WorldPopulation>(memory_), loading, simulation_,
      presentation_, random, *physics_, memory_);
}
void WorldRuntime::bind_physics(const MissionLoading& loading) {
  if (physics_) throw std::logic_error("Mission physics is already bound");
  // Explicit admission budgets for the source's fifty active characters,
  // shared hit shapes and skeletal bodies. These are storage limits, not
  // movement, material or solver tuning constants.
  physics::Configuration configuration;
  constexpr std::size_t kBodies = 4096, kShapes = 2048, kConstraints = 1024,
                        kBodyPairs = 32768, kContacts = 16384, kCommands = 8192,
                        kScratchBytes = 32U << 20, kQueryInstances = 4096;
  configuration.limits = {.bodies = kBodies,
                          .shapes = kShapes,
                          .constraints = kConstraints,
                          .body_pairs = kBodyPairs,
                          .contacts = kContacts,
                          .commands = kCommands,
                          .scratch_bytes = kScratchBytes,
                          .query_instances = kQueryInstances};
  physics_ = std::allocate_shared<WorldPhysics>(
      memory::Allocator<WorldPhysics>(memory_), clock_, collision_,
      loading.physics_recipes().scene, configuration, memory_);
}
void WorldRuntime::bind_effects(const MissionLoading& loading,
                                effects::ParticleRandom& random) {
  if (effects_) throw std::logic_error("Mission effects are already bound");
  if (!physics_) throw std::logic_error("Mission effects need world physics");
  // This encounter has no radial force producers; original setup seeds the
  // shared effect base force to zero. Capacity is admission storage, not an
  // authored particle count or emission-rate override.
  constexpr std::size_t kEffectInstanceCapacity = 512;
  auto candidate = std::allocate_shared<CombatEffects>(
      memory::Allocator<CombatEffects>(memory_), memory_, clock_, random,
      loading.combat_effect_packs(), loading.combat_effect_ids(),
      presentation_.effects_publisher(), *physics_,
      effects::SpriteWorldConfiguration{kEffectInstanceCapacity});
  presentation_.configure_effects(candidate->capacity());
  effects_ = std::move(candidate);
}
void WorldRuntime::bind_combat(const MissionLoading& loading,
                               SourceRandom& random, audio::CueOutput& audio,
                               CharacterAudioLookup character_audio,
                               PlayerDamageResources player_damage,
                               PlayerDefeatResources player_defeat) {
  if (combat_ || !physics_ || !population_ || !effects_ || !voices_) {
    throw std::logic_error("Combat requires unbound, admitted world services");
  }
  // Fixed admission pool accommodates the native fifty-actor population plus
  // Alicia. Its size is storage policy, not source light attenuation/rate.
  constexpr std::size_t kWeaponLightCapacity = 51;
  presentation_.configure_dynamic_lights(kWeaponLightCapacity);
  combat_ = std::allocate_shared<WorldCombat>(
      memory::Allocator<WorldCombat>(memory_), clock_, loading, simulation_,
      *population_, *physics_, presentation_, *effects_, random, audio,
      character_audio, player_damage, player_defeat, collision_, *voices_,
      memory_);
  simulation_.bind_sound_events(combat_);
}
std::size_t WorldRuntime::population_count() const {
  return population_ ? population_->active_count() : 0;
}
std::size_t WorldRuntime::pending_population_count() const {
  return population_ ? population_->pending_count() : 0;
}
void WorldRuntime::bind_audio(const PreparedCharacterAudio& prepared,
                              audio::CueOutput& output, SourceRandom& random,
                              CharacterVoicePool& voices) {
  if (voices_) throw std::logic_error("World audio is already bound");
  simulation_.bind_audio(prepared, output, random, voices);
  voices_ = &voices;
}
void WorldRuntime::change_costume(MissionLoading& loading, unsigned costume) {
  simulation_.change_costume(loading, costume, presentation_);
}
void WorldRuntime::change_weapon(MissionLoading& loading,
                                 unsigned weapon_class) {
  simulation_.replace_weapon(loading, weapon_class, false, presentation_);
}
void WorldRuntime::switch_weapon(MissionLoading& loading,
                                 unsigned weapon_class) {
  simulation_.replace_weapon(loading, weapon_class, true, presentation_);
}
AnimationReviewResult WorldRuntime::review(AnimationReviewRequest request) {
#if BW_DEVELOPER_VIEWS
  return review_->review(request);
#else
  (void)request;
  return {};
#endif
}
}  // namespace bw::game
