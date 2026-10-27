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
    : simulation_(clock, control, stage.definition.get(), customization,
                  background, player, domain, progress, camera_pose,
                  inspection_placement),
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
  if (!simulation_.advance(frame, input, presentation_.actor_publisher())) {
    return;
  }
  if (population_) population_->advance(frame.delta_seconds());
  if (const auto far =
          presentation_.update_environment(simulation_.presentation_camera())) {
    simulation_.set_camera_far(*far);
  }
  presentation_.present(simulation_.presentation_camera(),
                        simulation_.player_forces_visibility(),
                        frame.simulation_seconds());
}
AliciaActionUpdate WorldRuntime::begin_player_scripted_jump(
    const AcceptedScriptedJump& request) {
  return simulation_.begin_player_scripted_jump(request);
}
void WorldRuntime::synchronize_pause() { simulation_.synchronize_pause(); }
void WorldRuntime::bind_action_resources(PlayerActionResources resources) {
  simulation_.bind_action_resources(resources);
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
  population_ = std::allocate_shared<WorldPopulation>(
      memory::Allocator<WorldPopulation>(memory_), loading, simulation_,
      presentation_, random, memory_);
}
std::size_t WorldRuntime::population_count() const {
  return population_ ? population_->active_count() : 0;
}
std::size_t WorldRuntime::pending_population_count() const {
  return population_ ? population_->pending_count() : 0;
}
void WorldRuntime::bind_audio(const PreparedCharacterAudio& prepared,
                              audio::CueOutput& output, SourceRandom& random) {
  simulation_.bind_audio(prepared, output, random);
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
