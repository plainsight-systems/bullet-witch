// Preserve original scene sampling and move its render packet distribution out
// of game presentation. Header contracts define receiver preparation/lifetime.
// Implementation consultation Core E.6 (#re-raii), performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md): candidate storage
// owns rollback; all draw storage is reserved before sealing and frame
// publication.
#include "bw/scene/lighting/lighting_publication.hpp"

#include <stdexcept>
#include <utility>

namespace bw::scene {
namespace {
// Preserve destination identity while replacing the independently sampled
// coefficients, reflected texture and direct-light packet as one observation.
void apply_packet(render::WorldLightingUpdate& destination,
                  const render::WorldLightingUpdate& packet) {
  destination.irradiance_coefficients = packet.irradiance_coefficients;
  destination.local_lights = packet.local_lights;
  destination.local_light_count = packet.local_light_count;
  destination.reflection_texture = packet.reflection_texture;
}
}  // namespace
LightingPublication::LightingPublication(
    const SceneLightingSource& source, const ReflectionBindings& reflections,
    render::LightingPublisher& output,
    const std::shared_ptr<memory::Domain>& domain)
    : scene_(source, domain),
      reflections_(reflections),
      output_(output),
      domain_(domain) {}
void LightingPublication::set_parameters(
    const SceneLightingParameters& parameters) {
  scene_.set_parameters(parameters);
}
void LightingPublication::set_field_intensity(LightingFieldId field,
                                              float intensity) {
  scene_.set_field_intensity(field, intensity);
}
void LightingPublication::set_view(const LocalLightViewQuery& view) {
  scene_.set_view(view);
}
void LightingPublication::replace_local_lights(
    std::span<const LocalLight> lights) {
  scene_.replace_local_lights(lights);
}
DirectionalLighting LightingPublication::directional_lighting() const noexcept {
  return scene_.directional_lighting();
}
// Convert engine-selected values to the existing portable render layout. This
// is a binding operation, not a new light model or normalization policy.
render::WorldLightingUpdate LightingPublication::packet(
    const ReceiverLighting& value) const {
  render::WorldLightingUpdate result;
  result.irradiance_coefficients = value.irradiance;
  result.reflection_texture = value.reflection_slot
                                  ? reflections_.at(*value.reflection_slot)
                                  : render::WorldTextureHandle{};
  const auto& selected = value.local_lights;
  result.local_light_count = static_cast<std::uint32_t>(selected.count);
  for (std::size_t index = 0; index < selected.count; ++index) {
    const auto& source = selected.lights.at(index);
    auto& destination = result.local_lights.at(index);
    destination.position = {source.position[0], source.position[1],
                            source.position[2], 1};
    destination.diffuse = {source.diffuse[0], source.diffuse[1],
                           source.diffuse[2], 1};
    destination.specular = {source.specular[0], source.specular[1],
                            source.specular[2], 1};
    destination.attenuation = {source.attenuation[0], source.attenuation[1],
                               source.attenuation[2],
                               selected.contribution_threshold};
  }
  return result;
}
render::WorldLightingUpdate LightingPublication::evaluate(
    const LightingReceiver& receiver) {
  return packet(scene_.evaluate(receiver));
}
ReceiverPublication LightingPublication::prepare_receiver(
    const LightingReceiver& receiver, std::size_t draw_capacity) {
  return {*this, receiver, std::nullopt, draw_capacity};
}
ReceiverPublication LightingPublication::prepare_placed(
    LightingFieldId field, const math::Vector3<float>& root, float radius,
    std::size_t draw_capacity) {
  return {*this,
          {.policy = ReceiverLightingPolicy::spatial,
           .model_radius = radius,
           .field = field},
          root,
          draw_capacity};
}
ReceiverPublication::ReceiverPublication(
    LightingPublication& owner, const LightingReceiver& receiver,
    std::optional<math::Vector3<float>> placed_root, std::size_t draw_capacity)
    : owner_(&owner),
      receiver_(receiver),
      placed_root_(placed_root),
      updates_(memory::Allocator<render::WorldLightingUpdate>(owner.domain_)),
      capacity_(draw_capacity) {
  updates_.reserve(capacity_);
  if (placed_root_) retained_.emplace(owner.scene_, receiver_.field);
  initial_ = sample(receiver_);
}
ReceiverPublication::ReceiverPublication(ReceiverPublication&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)),
      receiver_(other.receiver_),
      placed_root_(other.placed_root_),
      retained_(std::move(other.retained_)),
      initial_(other.initial_),
      updates_(std::move(other.updates_)),
      capacity_(other.capacity_),
      bound_(other.bound_) {}
render::WorldLightingUpdate ReceiverPublication::sample(
    const LightingReceiver& receiver) {
  if (retained_) {
    if (!placed_root_) {
      throw std::logic_error("Placed receiver has no root observation");
    }
    return owner_->packet(
        retained_->evaluate(*placed_root_, receiver.model_radius));
  }
  return owner_->evaluate(receiver);
}
void ReceiverPublication::add_draw(render::WorldColorDraw& draw) {
  if (owner_ == nullptr || bound_) {
    throw std::logic_error("Lighting receiver is not preparing");
  }
  if (updates_.size() == capacity_) {
    throw std::length_error("Lighting receiver draw capacity exceeded");
  }
  auto update = initial_;
  update.model = draw.model;
  update.draw = draw.draw;
  update.instance = draw.instance;
  updates_.push_back(update);
  draw.reflection_texture = initial_.reflection_texture;
  draw.parameters.irradiance_coefficients = initial_.irradiance_coefficients;
  draw.parameters.local_lights = initial_.local_lights;
  draw.parameters.local_light_meta[0] = initial_.local_light_count;
}
void ReceiverPublication::seal() {
  if (owner_ == nullptr || bound_) {
    throw std::logic_error("Lighting receiver is not preparing");
  }
  bound_ = true;
}
void ReceiverPublication::require_bound() const {
  if (owner_ == nullptr || !bound_) {
    throw std::logic_error("Lighting receiver has no bound destinations");
  }
}
void ReceiverPublication::publish_packet(
    const render::WorldLightingUpdate& packet) {
  for (auto& update : updates_) apply_packet(update, packet);
  if (!updates_.empty()) owner_->output_.publish(updates_);
}
void ReceiverPublication::publish() {
  require_bound();
  if (!updates_.empty()) publish_packet(sample(receiver_));
}
void ReceiverPublication::publish_model(const math::Affine3<float>& transform) {
  require_bound();
  if (retained_ || receiver_.policy != ReceiverLightingPolicy::spatial) {
    throw std::logic_error("Receiver does not admit model transforms");
  }
  auto observation = receiver_;
  observation.model_world = transform;
  const auto packet = sample(observation);
  publish_packet(packet);
  receiver_ = observation;
}
}  // namespace bw::scene
