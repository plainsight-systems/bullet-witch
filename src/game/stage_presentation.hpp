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
#include "bw/render/material_animation.hpp"
#include "bw/render/render_scene.hpp"
#include "bw/render/shadow.hpp"
#include "bw/scene/lighting/scene_lighting.hpp"
#include "bw/scene/visibility/shadow_casters.hpp"
#include "camera/initial_camera.hpp"
#include "player_actor.hpp"
#include "stage_environment.hpp"
#include "stage_source.hpp"
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
  std::size_t selected_draws() const { return selected_draws_; }
  std::size_t omitted_draws() const { return omitted_draws_; }
  std::size_t sky_draws() const { return sky_draws_; }
  std::size_t background_draws() const { return background_draws_; }
  std::size_t player_draws() const { return player_draws_; }
  StagePresentation(const StagePresentation&) = delete;
  StagePresentation& operator=(const StagePresentation&) = delete;
  StagePresentation(StagePresentation&&) = delete;
  StagePresentation& operator=(StagePresentation&&) = delete;

 private:
  render::RenderScene output_;
  const PlayerActor* player_;
  std::shared_ptr<memory::Domain> memory_;
  float initial_far_distance_{};
  void append_player_draws(memory::Vector<render::WorldColorDraw>&,
                           const PlayerActor&, bool,
                           const InitialPlayerCamera&);
  // Scene owns fields and emitter selection; game maps authored field slots
  // and supplies model observations. Controllers never own lighting state.
  std::optional<scene::ReflectionField> reflection_field_;
  std::array<render::WorldTextureHandle, scene::kReflectionSlotCount>
      reflection_textures_{};
  std::optional<scene::SceneLighting> lighting_;
  scene::LightingFieldId character_field_, enemy_field_;
  memory::Vector<render::WorldLightingUpdate> static_lighting_updates_;
  memory::Vector<render::WorldVisibilityUpdate> static_shadow_updates_;
  // Each placed model owns a strict-field cache and a contiguous draw-packet
  // range. Instance IDs isolate copies of the same resident mesh. SceneLighting
  // is declared first and therefore outlives these borrowed receiver owners.
  // I.1/R.20/MEM.9: fixed observations, explicit lifetime, loading allocation.
  struct PlacedLighting {
    scene::RetainedLightingReceiver receiver;
    math::Vector3<float> root_position{};
    float radius{};
    std::size_t first_update{}, update_count{};
  };
  memory::Vector<PlacedLighting> placed_lighting_;
  memory::Vector<render::WorldLightingUpdate> placed_lighting_updates_;
  render::WorldLightingUpdate model_lighting(
      const ResidentModelView& resident, const animation::Matrix& transform);
  void publish_player_lighting();
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
