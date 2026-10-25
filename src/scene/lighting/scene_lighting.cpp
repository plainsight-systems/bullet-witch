// Compose recovered spatial samplers under scene ownership. The header defines
// publication/lifetime and cost contracts. Implementation: Core I.1
// (#ri-explicit) rejects absent policies/fields before producing a packet;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// confines allocation to preparation and explicit emitter replacement.
// Redesign implementation I.1 (#ri-explicit), R.20 (#rr-owner), CDSA.32 and
// MEM.9 retain distinct source receiver policies over shared prepared data.
// Per-instance cached SH never replaces per-observation direct-light selection.
#include "bw/scene/lighting/scene_lighting.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bw::scene {
namespace {
// Static geometry and sky use the original constant-field construction, not a
// spatial fallback. Preserve its two multiplications and seven-vector layout.
PackedIrradiance constant_irradiance(const math::Vector3<float>& color) {
  constexpr float kConstantHarmonicScale = 3.544907569885254F;
  constexpr float kConstantPolynomialScale = 0.2820947766304016F;
  PackedIrradiance result{};
  for (std::size_t channel = 0; channel < color.size(); ++channel) {
    const float harmonic = color.at(channel) * kConstantHarmonicScale;
    result.at(channel)[3] = harmonic * kConstantPolynomialScale;
  }
  result.back()[3] = 1;
  return result;
}
// Overflow light coefficients already contain emitter attenuation and source
// projection. Add them after spatial file/owner scaling, then pack once; they
// must not inherit the field intensity or the retained receiver cache cadence.
PackedIrradiance receiver_irradiance(IrradianceCoefficients sampled,
                                     const SelectedLocalLights& lights) {
  for (std::size_t coefficient = 0; coefficient < sampled.size();
       ++coefficient) {
    for (std::size_t channel = 0; channel < sampled.at(coefficient).size();
         ++channel) {
      sampled.at(coefficient).at(channel) +=
          lights.residual_irradiance.at(coefficient).at(channel);
    }
  }
  return pack_irradiance(sampled);
}
void validate_receiver(const LightingReceiver& receiver) {
  if (!math::all_finite(receiver.model_center) ||
      !std::isfinite(receiver.model_radius) || receiver.model_radius < 0 ||
      !std::ranges::all_of(
          receiver.model_world,
          [](const auto& row) { return math::all_finite(row); })) {
    throw std::invalid_argument("Invalid lighting receiver geometry");
  }
}
// Prepare the authored directional command once at scene publication. Preserve
// the recovered Z bias and operation order; this is not exposure compensation.
DirectionalLighting prepare_directional(
    const DirectionalLightDefinition& light) {
  if (!math::all_finite(light.vector) || !math::all_finite(light.diffuse) ||
      !math::all_finite(light.specular)) {
    throw std::invalid_argument("Invalid authored directional light");
  }
  constexpr float kDirectionBias = 0.001F;
  const float x = light.vector[0];
  const float y = light.vector[1];
  const float z = light.vector[2] + kDirectionBias;
  const float magnitude = std::sqrt(x * x + y * y + z * z);
  if (!std::isfinite(magnitude) || magnitude == 0) {
    throw std::invalid_argument("Invalid authored directional light vector");
  }
  return {{-x / magnitude, -y / magnitude, -z / magnitude, 0},
          light.diffuse,
          light.specular};
}
}  // namespace
SceneLighting::SceneLighting(const SceneLightingSource& source,
                             const std::shared_ptr<memory::Domain>& domain)
    : reflections_(source.reflections),
      domain_(domain),
      fields_(memory::Allocator<Field>(domain)),
      local_lights_(source.lights, source.local_settings, domain),
      local_settings_(source.local_settings) {
  set_parameters(source.parameters);
  fields_.reserve(source.fields.size());
  for (const auto& field : source.fields) {
    if (!std::isfinite(field.intensity)) {
      throw std::invalid_argument("Invalid spatial lighting intensity");
    }
    fields_.push_back({IrradianceField(field.bytes, domain), field.intensity});
  }
}
void SceneLighting::set_parameters(const SceneLightingParameters& parameters) {
  if (!math::all_finite(parameters.ambient)) {
    throw std::invalid_argument("Invalid scene lighting parameters");
  }
  const auto directional = prepare_directional(parameters.directional);
  parameters_ = parameters;
  directional_ = directional;
}
DirectionalLighting SceneLighting::directional_lighting() const noexcept {
  return directional_;
}
void SceneLighting::set_field_intensity(LightingFieldId field,
                                        float intensity) {
  if (!std::isfinite(intensity)) {
    throw std::invalid_argument("Invalid spatial lighting intensity");
  }
  fields_.at(field.index).intensity = intensity;
}
void SceneLighting::set_view(const LocalLightViewQuery& view) {
  // Resolve a complete new view before replacing the prior observation. Static
  // material draws share this source packet; actor queries remain independent.
  const auto selected = local_lights_.select_view(view);
  view_ = view;
  view_lights_ = selected;
}
void SceneLighting::replace_local_lights(std::span<const LocalLight> lights) {
  // Candidate construction may reject or exhaust the domain. Until it succeeds,
  // the currently published emitter set and its prepared scratch remain intact.
  auto candidate = LocalLights(lights, local_settings_, domain_);
  const auto selected =
      view_ ? candidate.select_view(*view_) : SelectedLocalLights{};
  local_lights_ = std::move(candidate);
  view_lights_ = selected;
}
ReceiverLighting SceneLighting::evaluate(const LightingReceiver& receiver) {
  validate_receiver(receiver);
  switch (receiver.policy) {
    case ReceiverLightingPolicy::stage_ambient:
      if (!view_) {
        throw std::logic_error("Static lighting view has not been published");
      }
      return {constant_irradiance(parameters_.ambient), view_lights_};
    case ReceiverLightingPolicy::sky_white:
      return {constant_irradiance({1, 1, 1}), {}};
    case ReceiverLightingPolicy::spatial:
      if (!view_) {
        throw std::logic_error("Spatial lighting view has not been published");
      }
      break;
    default:
      throw std::invalid_argument("Unclassified lighting receiver policy");
  }
  // A model and its attachments each supply their submitted transform. Neither
  // camera-relative coordinates nor controller position replaces this center.
  const auto center =
      math::transform_point<float>(receiver.model_center, receiver.model_world);
  const auto& field = fields_.at(receiver.field.index);
  const auto lights =
      local_lights_.select_active({center, receiver.model_radius});
  return {
      receiver_irradiance(field.data.sample(center, field.intensity), lights),
      lights, reflections_ ? reflections_->slot_at(center) : std::nullopt};
}
RetainedLightingReceiver::RetainedLightingReceiver(SceneLighting& scene,
                                                   LightingFieldId field)
    : scene_(scene), field_(field) {
  if (field_.index >= scene.fields_.size()) {
    throw std::out_of_range("Placed lighting field identity exceeds scene");
  }
}
ReceiverLighting RetainedLightingReceiver::evaluate(
    const math::Vector3<float>& root_position, float radius) {
  auto& scene = scene_.get();
  if (!scene.view_) {
    throw std::logic_error("Placed lighting view has not been published");
  }
  if (!math::all_finite(root_position) || !std::isfinite(radius) ||
      radius < 0) {
    throw std::invalid_argument("Invalid placed lighting observation");
  }
  const auto& field = scene.fields_.at(field_.index);
  constexpr float kIntensityCoordinateScale = 10;
  constexpr float kInvalidationDistanceSquared = .25F;
  const math::Vector<float, 4> key{root_position[0], root_position[1],
                                   root_position[2],
                                   kIntensityCoordinateScale * field.intensity};
  if (!math::all_finite(key)) {
    throw std::invalid_argument("Placed lighting intensity key overflow");
  }
  bool refresh = !key_;
  if (key_) {
    const auto delta = math::subtract(*key_, key);
    const float distance_squared = delta[1] * delta[1] + delta[0] * delta[0] +
                                   delta[2] * delta[2] + delta[3] * delta[3];
    refresh = distance_squared >= kInvalidationDistanceSquared;
  }
  // Local-light membership changes independently of the retained SH threshold.
  const auto lights =
      scene.local_lights_.select_active({root_position, radius});
  if (refresh) {
    const auto candidate = field.data.sample(
        root_position, field.intensity, IrradianceSampling::first_containing);
    const auto reflection = scene.reflections_
                                ? scene.reflections_->slot_at(root_position)
                                : std::nullopt;
    irradiance_ = candidate;
    reflection_slot_ = reflection;
    key_ = key;
  }
  return {receiver_irradiance(irradiance_, lights), lights, reflection_slot_};
}
}  // namespace bw::scene
