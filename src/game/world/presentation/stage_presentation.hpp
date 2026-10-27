// Slice 2 design F.7 (#rf-smart), C.32 (#rc-dtor-ptr), COPY.4 and CDSA.32:
// scene-owned receiver publication borrows stable output; game policy supplies
// authored bounds/fields/channels. Prepared destination storage moves without
// throwing after native appearance replacement.
// Ownership split of the existing stage runtime; no new simulation algorithm.
// Design Core I.1 (#ri-explicit), C.9 (#rc-private) and R.21 (#rr-unique)
// keep authority private and borrows explicit. Implementation E.6 (#re-raii)
// preserves destruction/unwind order. Both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) reserves domain
// buffers during preparation; frame operations retain their bounded fixed
// footprint. COPY.4 (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md)
// keeps stable owner identities noncopyable/nonmovable. See
// docs/architecture/reorganization-plan.md. Own retained scene registration,
// authored environment/light receivers and material channels. Borrow only const
// player observations and mission assets; camera observations are values
// supplied by simulation, never mutable pointers. Prepare once, sample
// environment, publish complete view/light/shadow packets, then sample each
// unique material curve at admitted world time. Own no clock, contact world,
// gameplay action or diagnostic state. RenderScene retires draws before asset
// owners die, including partial-construction failure. Single thread.
#pragma once
#include "bw/lighting/lighting_publication.hpp"
#include "bw/render/materials/material_animation.hpp"
#include "bw/render/scene/render_scene.hpp"
#include "bw/render/shadows/shadow.hpp"
#include "bw/render/shadows/shadow_casters.hpp"
#include "game/actors/alicia/player_actor.hpp"
#include "game/actors/npc/npc_actor.hpp"
#include "game/player/camera/initial_camera.hpp"
#include "game/world/stage_source.hpp"
#include "stage_environment.hpp"
namespace bw::game {
// Setup-only copied observations distinguish snapped environment admission from
// completed follow-camera publication. Neither is a mutable camera capability.
struct StageCameraAdmission {
  InitialPlayerCamera initial;
  InitialPlayerCamera completed;
};
class StagePresentation {
 public:
  ~StagePresentation() = default;
  StagePresentation(render::WorldPresentation&, StageViewSource,
                    StageViewSource, StageBackgroundSource,
                    StageCharacterLightSource,
                    const formats::CustomizationDefinition&, const PlayerActor*,
                    const StageCameraAdmission&, bool force_player_visibility,
                    const std::shared_ptr<memory::Domain>&);
  render::ActorPublisher& actor_publisher() {
    return output_.actor_publisher();
  }
  std::optional<float> update_environment(const InitialPlayerCamera&);
  float initial_far_distance() const { return initial_far_distance_; }
  void present(const InitialPlayerCamera& camera, bool force_player_visibility,
               double world_seconds);
  // Candidate storage must remain alive through stable actor adoption. Validate
  // all candidate draws, atomically replace, then update the copied draw count.
  void replace_appearance(const PlayerActor& candidate, bool weapon,
                          const InitialPlayerCamera&);
  // Borrow stable actor observations through explicit retirement. Admission
  // prepares lighting/draws and publishes one transactional instance batch.
  // World owns actor lifetime; no actor mutation or clock capability escapes.
  void admit_npc(const NpcActor&, const InitialPlayerCamera&);
  void retire_npc(PopulationIdentity);
  std::size_t selected_draws() const { return selected_draws_; }
  std::size_t omitted_draws() const { return omitted_draws_; }
  std::size_t sky_draws() const { return sky_draws_; }
  std::size_t background_draws() const { return background_draws_; }
  std::size_t player_draws() const { return player_draws_; }
  void set_player_visible(bool visible, const InitialPlayerCamera& camera,
                          bool force_player_visibility);
  StagePresentation(const StagePresentation&) = delete;
  StagePresentation& operator=(const StagePresentation&) = delete;
  StagePresentation(StagePresentation&&) = delete;
  StagePresentation& operator=(StagePresentation&&) = delete;

 private:
  render::RenderScene output_;
  const PlayerActor* player_;
  bool player_visible_{true};
  std::shared_ptr<memory::Domain> memory_;
  float initial_far_distance_{};
  scene::ReceiverPublication append_player_draws(
      memory::Vector<render::WorldColorDraw>&, const PlayerActor&, bool,
      const InitialPlayerCamera&);
  struct ActorDrawVisibility {
    std::function<bool(std::size_t)> selected;
    std::function<bool(std::size_t)> visible;
  };
  scene::ReceiverPublication append_actor_draws(
      memory::Vector<render::WorldColorDraw>&, const ResidentModelView&,
      const animation::Matrix&, std::uint64_t instance,
      const InitialPlayerCamera&, scene::LightingFieldId,
      const ActorDrawVisibility& visibility,
      const std::function<
          std::span<const render::WorldJointMatrix>(std::size_t)>& palette,
      std::span<const render::WorldTextureOverride> overrides = {});
  struct NpcReceiver {
    const NpcActor* actor{};
    std::array<std::optional<scene::ReceiverPublication>, 2> lighting;
  };
  static constexpr std::size_t kNpcCapacity = 50;
  std::array<NpcReceiver, kNpcCapacity> npc_receivers_{};
  void publish_npc_lighting();
  void publish_npc_visibility(const NpcActor&, bool weapon, bool casts_shadow);
  // Scene owns fields and emitter selection; game maps authored field slots
  // and supplies model observations. Controllers never own lighting state.
  std::optional<scene::ReflectionField> reflection_field_;
  std::optional<scene::LightingPublication> lighting_;
  scene::LightingFieldId character_field_, enemy_field_;
  // Receiver publication owns prepared destination packets and cache state.
  // They retire before the scene sampler, which retires before RenderScene.
  std::optional<scene::ReceiverPublication> static_lighting_;
  memory::Vector<scene::ReceiverPublication> placed_lighting_;
  std::array<std::optional<scene::ReceiverPublication>, 2> player_lighting_;
  memory::Vector<render::WorldVisibilityUpdate> static_shadow_updates_;
  scene::LightingReceiver model_receiver(
      const ResidentModelView& resident,
      const animation::Matrix& transform) const;
  void publish_player_lighting();
  void publish_player_visibility(bool weapon, bool casts_shadow);
  void publish_shadow_owners(const InitialPlayerCamera& camera,
                             bool force_player_visibility);
  void publish_view(const InitialPlayerCamera&, bool force_player_visibility);
  std::optional<formats::StageEnvironment> environment_;
  std::optional<scene::Environment> environment_channels_;
  render::WorldViewUpdate::ScenePasses scene_passes_;
  std::optional<render::ShadowProjection> shadow_projection_;
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
