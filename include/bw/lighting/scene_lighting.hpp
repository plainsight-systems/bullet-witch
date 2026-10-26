// Scene-owned prepared lighting, independent of game controllers and devices.
// Construction borrows field bytes/emitter spans only for the call, then owns
// decoded fields and copied lights in the supplied nonnull memory domain.
// Optional immutable ReflectionField is borrowed until destruction and must
// outlive this scene; its per-position slot is copied into each receiver
// packet. A receiver publishes source group-zero bounds and the final model
// transform; its explicit policy selects stage ambient, white sky or a spatial
// field. Spatial evaluation transforms the local center once, retains the
// authored unscaled radius, samples the selected field with its live owner
// intensity, packs the original polynomial and selects the source direct-light
// packet. Static evaluation combines its constant polynomial with the published
// view light packet; spatial selection uses that view's active emitters. Sky
// uses its white polynomial and no local lights. View must be prepared before
// evaluating static or spatial receivers. Field identities are scene-local
// source-array indices, stable until owner destruction; game labels stay
// outside. Missing required fields, invalid geometry/settings and nonfinite
// values throw before a result is returned. Signed intensities/colors are not
// normalized. Results own fixed values and may outlive this owner. Queries and
// updates are owner-thread operations: local selection reuses prepared mutable
// scratch. Construction/replacing emitters allocates; successful evaluation and
// parameter updates do not. Replacement validates a candidate before replacing
// live data. Storage/work are those of IrradianceField and LocalLights, plus
// fixed packet data; repeated material draws reuse one result per model
// observation. Source/ref boundary:
// research/engine/subsystems/lighting-reference-boundaries.md. Design: Core I.1
// (#ri-explicit) makes policies, lifetime and publication explicit; R.20
// (#rr-owner) retains the shared allocation domain. Performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) separates preparation
// from the allocation-free query path. No GPU/API or game-type dependency.
// Placed receivers retain a separate strict-zone irradiance observation. Their
// root XYZ and ten times live field intensity form the original invalidation
// key; squared change >= .25 resamples, while direct lights update every call.
// Optional overflow SH adds after field/owner scaling on each observation;
// this dynamic residual is never cached with the spatial sample.
// RetainedLightingReceiver borrows this stable, nonmovable scene and must die
// first. It exposes only receiver observation, not scene mutation. Each object
// owns its fixed cache; move transfers that object state, copying is forbidden.
// Both redesign phases: I.1 makes sampling/authority explicit, R.20 owns domain
// storage, MEM.9 retains allocation-free observations; CDSA.32 shares prepared
// field data between blended and strict sampling. See lighting.md.
#pragma once
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>

#include "irradiance_field.hpp"
#include "local_lights.hpp"
#include "reflection_field.hpp"

namespace bw::scene {
enum class ReceiverLightingPolicy : std::uint8_t {
  unclassified,
  stage_ambient,
  sky_white,
  spatial
};
struct LightingFieldId {
  std::size_t index{std::numeric_limits<std::size_t>::max()};
};
struct LightingReceiver {
  ReceiverLightingPolicy policy{ReceiverLightingPolicy::unclassified};
  math::Vector3<float> model_center{};
  float model_radius{};
  math::Affine3<float> model_world{
      {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}}};
  LightingFieldId field{};
};
struct DirectionalLightDefinition {
  math::Vector3<float> vector{};
  math::Vector<float, 4> diffuse{};
  math::Vector<float, 4> specular{};
};
// Shared scene lighting is distinct from a receiver's spatial observation.
// Publication preserves authored color components and prepares the original
// negated, normalized direction after its Z bias. No exposure or PBR conversion
// is applied here. Returned values own storage and survive later publication.
struct DirectionalLighting {
  math::Vector<float, 4> direction{};
  math::Vector<float, 4> diffuse{};
  math::Vector<float, 4> specular{};
};
struct SceneLightingParameters {
  math::Vector3<float> ambient{};
  DirectionalLightDefinition directional{};
};
struct SceneLightFieldSource {
  std::span<const std::uint8_t> bytes;
  float intensity{1};
};
struct SceneLightingSource {
  std::span<const SceneLightFieldSource> fields;
  std::span<const LocalLight> lights;
  LocalLightSettings local_settings;
  SceneLightingParameters parameters;
  // Immutable spatial selector borrows the scene lifetime and must outlive
  // this owner and its receiver caches. Absence publishes opaque black.
  const ReflectionField* reflections{};
};
struct ReceiverLighting {
  PackedIrradiance irradiance{};
  SelectedLocalLights local_lights;
  std::optional<std::uint32_t> reflection_slot{};
};
class RetainedLightingReceiver;
class SceneLighting {
 public:
  SceneLighting(const SceneLightingSource& source,
                const std::shared_ptr<memory::Domain>& domain);
  SceneLighting(const SceneLighting&) = delete;
  SceneLighting& operator=(const SceneLighting&) = delete;
  SceneLighting(SceneLighting&&) = delete;
  SceneLighting& operator=(SceneLighting&&) = delete;
  ~SceneLighting() = default;

  // Resolve a complete independent model observation; no cached receiver state
  // can leak between body, attachment or other entity queries.
  ReceiverLighting evaluate(const LightingReceiver& receiver);
  [[nodiscard]] DirectionalLighting directional_lighting() const noexcept;
  // Explicit source publication boundaries, serialized against evaluate().
  void set_parameters(const SceneLightingParameters& parameters);
  void set_field_intensity(LightingFieldId field, float intensity);
  void set_view(const LocalLightViewQuery& view);
  void replace_local_lights(std::span<const LocalLight> lights);

 private:
  friend class RetainedLightingReceiver;
  const ReflectionField* reflections_{};
  std::shared_ptr<memory::Domain> domain_;
  struct Field {
    IrradianceField data;
    float intensity{};
  };
  memory::Vector<Field> fields_;
  LocalLights local_lights_;
  LocalLightSettings local_settings_;
  SceneLightingParameters parameters_;
  DirectionalLighting directional_;
  std::optional<LocalLightViewQuery> view_;
  SelectedLocalLights view_lights_;
};
// One placed object's retained strict-field observation. The scene outlives the
// receiver; root position comes from the original control-point/object
// publisher, not a transformed actor group center. Invalid observations
// preserve the cache.
class RetainedLightingReceiver {
 public:
  RetainedLightingReceiver(SceneLighting& scene, LightingFieldId field);
  RetainedLightingReceiver(const RetainedLightingReceiver&) = delete;
  RetainedLightingReceiver& operator=(const RetainedLightingReceiver&) = delete;
  RetainedLightingReceiver(RetainedLightingReceiver&&) = default;
  RetainedLightingReceiver& operator=(RetainedLightingReceiver&&) = delete;
  ~RetainedLightingReceiver() = default;
  ReceiverLighting evaluate(const math::Vector3<float>& root_position,
                            float radius);

 private:
  std::reference_wrapper<SceneLighting> scene_;
  LightingFieldId field_;
  std::optional<math::Vector<float, 4>> key_;
  IrradianceCoefficients irradiance_{};
  std::optional<std::uint32_t> reflection_slot_{};
};
}  // namespace bw::scene
