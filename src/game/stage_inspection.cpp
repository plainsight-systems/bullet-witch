// Implement the owner contract in the matching header. Design I.1/C.9 keeps
// authoritative state private; implementation E.6 (#re-raii) preserves cleanup
// on failed preparation. Both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps retained buffers
// fixed during admitted frames. Existing source algorithms and ordering remain.
#include "stage_inspection.hpp"
namespace bw::game {
StageInspection::StageInspection(
    const world::WorldClock& clock, const MissionControl& control,
    render::WorldPresentation& output, StageViewSource stage,
    StageViewSource sky, const std::shared_ptr<memory::Domain>& domain,
    const std::optional<render::ViewPose>& camera_pose,
    StageBackgroundSource background,
    const std::optional<PlayerActorSource>& player,
    const content::LoadProgress& progress, StageCharacterLightSource light,
    const std::optional<ScriptedPlayerPose>& inspection_placement)
    : StageInspection(
          clock, control, output, stage, sky, domain, camera_pose, background,
          player, progress, light, inspection_placement,
          formats::CustomizationDefinition::parse(
              stage.definition.get().customization_bytes(), domain)) {}
StageInspection::StageInspection(
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
      review_(simulation_.make_review(presentation_.actor_publisher(),
                                      customization)) {
  simulation_.set_camera_far(presentation_.initial_far_distance());
}
StageInspection::~StageInspection() = default;
void StageInspection::advance(const world::WorldFrame& frame,
                              StageTraversalInput input) {
  if (!simulation_.advance(frame, input, presentation_.actor_publisher(),
                           review_)) {
    return;
  }
  if (const auto far =
          presentation_.update_environment(simulation_.presentation_camera())) {
    simulation_.set_camera_far(*far);
  }
  presentation_.present(simulation_.presentation_camera(),
                        simulation_.player_forces_visibility(),
                        frame.simulation_seconds());
}
AliciaActionUpdate StageInspection::begin_player_scripted_jump(
    const AcceptedScriptedJump& request) {
  return simulation_.begin_player_scripted_jump(request);
}
void StageInspection::synchronize_pause() { simulation_.synchronize_pause(); }
void StageInspection::bind_action_resources(PlayerActionResources resources) {
  simulation_.bind_action_resources(resources);
}
void StageInspection::bind_audio(const PreparedCharacterAudio& prepared,
                                 audio::CueOutput& output,
                                 SourceRandom& random) {
  simulation_.bind_audio(prepared, output, random);
}
void StageInspection::change_costume(MissionLoading& loading,
                                     unsigned costume) {
  simulation_.change_costume(loading, costume, presentation_);
}
void StageInspection::change_weapon(MissionLoading& loading,
                                    unsigned weapon_class) {
  simulation_.replace_weapon(loading, weapon_class, false, presentation_,
                             review_);
}
void StageInspection::switch_weapon(MissionLoading& loading,
                                    unsigned weapon_class) {
  simulation_.replace_weapon(loading, weapon_class, true, presentation_,
                             review_);
}
AnimationReviewResult StageInspection::review(AnimationReviewRequest request) {
  return review_.review(request);
}
}  // namespace bw::game
