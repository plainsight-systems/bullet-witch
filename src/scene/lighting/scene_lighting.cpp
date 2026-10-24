// Compose recovered spatial samplers under scene ownership. The header defines
// publication/lifetime and cost contracts. Implementation: Core I.1
// (#ri-explicit) rejects absent policies/fields before producing a packet;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// confines allocation to preparation and explicit emitter replacement.
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
    : domain_(domain),
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
  return {pack_irradiance(field.data.sample(center, field.intensity)),
          local_lights_.select_active({center, receiver.model_radius})};
}
}  // namespace bw::scene
