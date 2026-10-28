// Compose recovered spatial samplers under scene ownership. The header defines
// publication/lifetime and cost contracts. Implementation: Core I.1
// (#ri-explicit) rejects absent policies/fields before producing a packet;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// confines allocation to preparation and explicit emitter replacement.
// Redesign implementation I.1 (#ri-explicit), R.20 (#rr-owner), CDSA.32 and
// MEM.9 retain distinct source receiver policies over shared prepared data.
// Per-instance cached SH never replaces per-observation direct-light selection.
// Publication implementation: Core I.1 retains completed query observations
// while producers stage values; MEM.9 bounds pending data by reserved slots.
#include "bw/lighting/scene_lighting.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <utility>

namespace bw::scene {
// Fixed scene registration pool. A capability keeps only a weak reference,
// never the scene alive. Allocation occurs once at scene preparation. E.4 and
// MEM.9 were read for generation, publication and allocation invariants.
struct LocalLightEmitter::State {
  struct Slot {
    std::uint64_t generation{};
    std::size_t next{};
    bool live{}, pending{}, reset_history{};
    std::optional<LocalLight> staged;
  };
  State(SceneLighting& scene, std::size_t count, const LocalLights& lights,
        const std::shared_ptr<memory::Domain>& domain)
      : owner(scene),
        slots(count, Slot{}, memory::Allocator<Slot>(domain)),
        first_static(lights.size()),
        thread(std::this_thread::get_id()) {
    for (std::size_t i = 0; i < count; ++i) {
      slots[i].next = i + 1;
    }
  }
  std::reference_wrapper<SceneLighting> owner;
  memory::Vector<Slot> slots;
  std::size_t first_static{}, free_head{};
  std::thread::id thread;
  void require_thread() const {
    if (thread != std::this_thread::get_id()) {
      throw std::logic_error("Local light capability used on another thread");
    }
  }
  bool valid(std::size_t slot, std::uint64_t generation) const noexcept {
    return generation && slot < slots.size() && slots[slot].live &&
           slots[slot].generation == generation;
  }
};
LocalLightEmitter::LocalLightEmitter(const std::shared_ptr<State>& state,
                                     std::size_t slot, std::uint64_t generation)
    : state_(state), slot_(slot), generation_(generation) {}
LocalLightEmitter::~LocalLightEmitter() { release(); }
LocalLightEmitter::LocalLightEmitter(LocalLightEmitter&& other) noexcept
    : state_(std::move(other.state_)),
      slot_(other.slot_),
      generation_(std::exchange(other.generation_, 0)) {}
LocalLightEmitter& LocalLightEmitter::operator=(
    LocalLightEmitter&& other) noexcept {
  if (this != &other) {
    release();
    state_ = std::move(other.state_);
    slot_ = other.slot_;
    generation_ = std::exchange(other.generation_, 0);
  }
  return *this;
}
bool LocalLightEmitter::valid() const noexcept {
  const auto state = state_.lock();
  return state && state->thread == std::this_thread::get_id() &&
         state->valid(slot_, generation_);
}
void LocalLightEmitter::update(const LocalLight& light) {
  const auto state = state_.lock();
  if (!state) {
    throw std::logic_error("Local light scene has retired");
  }
  state->require_thread();
  if (!state->valid(slot_, generation_)) {
    throw std::logic_error("Stale local light capability");
  }
  state->owner.get().update_emitter(state->first_static + slot_, light);
}
void LocalLightEmitter::clear() {
  const auto state = state_.lock();
  if (!state) {
    return;
  }
  state->require_thread();
  if (!state->valid(slot_, generation_)) {
    throw std::logic_error("Stale local light capability");
  }
  state->owner.get().update_emitter(state->first_static + slot_, std::nullopt);
}
void LocalLightEmitter::release() noexcept {
  const auto state = state_.lock();
  if (state && generation_) {
    if (state->thread != std::this_thread::get_id() ||
        !state->valid(slot_, generation_)) {
      std::terminate();
    }
    // Generation/thread validation above makes this fixed-slot retirement
    // infallible; destruction never calls the throwing producer validator.
    auto& slot = state->slots[slot_];
    slot.staged.reset();
    slot.pending = true;
    slot.live = false;
    slot.reset_history = true;
    slot.next = state->free_head;
    state->free_head = slot_;
  }
  state_.reset();
  generation_ = 0;
}
SceneLighting::~SceneLighting() { emitter_state_.reset(); }
void SceneLighting::prepare_emitters(std::size_t capacity) {
  if (emitter_state_) {
    throw std::logic_error("Local light capacity already prepared");
  }
  auto state = std::allocate_shared<LocalLightEmitter::State>(
      memory::Allocator<LocalLightEmitter::State>(domain_), *this, capacity,
      local_lights_, domain_);
  local_lights_.reserve_dynamic(capacity);
  emitter_state_ = std::move(state);
  view_.reset();
}
LocalLightEmitter SceneLighting::acquire_emitter() {
  if (!emitter_state_) {
    throw std::logic_error("Local light capacity not prepared");
  }
  auto& state = *emitter_state_;
  state.require_thread();
  if (state.free_head == state.slots.size()) {
    throw std::length_error("Local light registration capacity exhausted");
  }
  const auto index = state.free_head;
  auto& slot = state.slots[index];
  if (slot.generation == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("Local light generation exhausted");
  }
  state.free_head = slot.next;
  ++slot.generation;
  slot.live = true;
  return LocalLightEmitter(emitter_state_, index, slot.generation);
}
void SceneLighting::update_emitter(std::size_t index,
                                   const std::optional<LocalLight>& light) {
  // Validate before replacing a previously accepted pending value. Updating a
  // producer must not revoke the completed view used by delayed admissions.
  local_lights_.validate_dynamic(index, light);
  auto& slot = emitter_state_->slots.at(index - emitter_state_->first_static);
  slot.staged = light;
  slot.pending = true;
}
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
  // Reject an invalid camera before committing any staged emitter. Accepted
  // values were already validated at their producer boundary; this fixed batch
  // cannot allocate. Cull/order exactly once, preserving source depth history.
  LocalLights::validate_view(view);
  if (emitter_state_) {
    auto& state = *emitter_state_;
    for (std::size_t index = 0; index < state.slots.size(); ++index) {
      auto& slot = state.slots[index];
      if (!slot.pending) continue;
      const auto destination = state.first_static + index;
      if (slot.reset_history) {
        local_lights_.update_dynamic(destination, std::nullopt);
      }
      local_lights_.update_dynamic(destination, slot.staged);
      slot.pending = false;
      slot.reset_history = false;
    }
  }
  const auto selected = local_lights_.select_view(view);
  view_ = view;
  view_lights_ = selected;
}
void SceneLighting::clear_static_emitters() {
  local_lights_.clear_static_emitters();
  if (view_) view_lights_ = local_lights_.select_view(*view_);
}
void SceneLighting::replace_local_lights(std::span<const LocalLight> lights) {
  if (emitter_state_) {
    throw std::logic_error("Cannot replace registered scene lights");
  }
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
