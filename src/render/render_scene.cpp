// Scene admission and retirement are one RAII responsibility; native retained
// draws remain authoritative. E.6 and MEM.9 were reread for implementation:
// validate lifecycle before forwarding, retain no copied packets or dynamic
// storage, and release the claim exactly once even during construction failure.
#include "bw/render/render_scene.hpp"

#include <stdexcept>

namespace bw::render {
RenderScene::RenderScene(WorldPresentation& output) : output_(output) {
  if (output_.scene_claimed_) {
    throw std::logic_error("World output already has a scene owner");
  }
  output_.scene_claimed_ = true;
}
RenderScene::~RenderScene() { retire(); }
void RenderScene::require_live() const {
  if (state_ == State::retired) {
    throw std::logic_error("Render scene is retired");
  }
}
void RenderScene::require_active() const {
  if (state_ != State::active) {
    throw std::logic_error("Render scene has no active registration");
  }
}
WorldGeometryStatus RenderScene::geometry_status(WorldModelHandle model) const {
  require_live();
  return output_.geometry_status(model);
}
void RenderScene::set_world_output_parameters(
    const WorldOutputParameters& parameters) {
  require_live();
  output_.set_world_output_parameters(parameters);
}
void RenderScene::register_draws(std::span<const WorldColorDraw> draws) {
  if (state_ != State::preparing) {
    throw std::logic_error("Render scene registration is not preparing");
  }
  // Native validation/preparation is transactional. A rejected registration
  // leaves this owner preparing and cannot acquire another scene's lifetime.
  output_.set_world_draws(draws);
  state_ = State::active;
}
void RenderScene::replace_model_draws(
    WorldModelHandle previous, std::span<const WorldColorDraw> replacement) {
  require_active();
  if (replacement.empty()) {
    throw std::invalid_argument("Use explicit scene removal for empty draws");
  }
  output_.replace_world_model_draws(previous, replacement);
}
void RenderScene::remove_model_draws(WorldModelHandle model) {
  require_active();
  output_.replace_world_model_draws(model, {});
}
void RenderScene::update_world_texture_offsets(
    std::span<const WorldTextureOffsetUpdate> updates) {
  require_active();
  output_.update_world_texture_offsets(updates);
}
void LightingPublisher::publish(std::span<const WorldLightingUpdate> updates) {
  scene_.require_active();
  scene_.output_.update_world_lighting(updates);
}
void RenderScene::update_world_visibility(
    std::span<const WorldVisibilityUpdate> updates) {
  require_active();
  output_.update_world_visibility(updates);
}
void RenderScene::update_world_view(const WorldViewUpdate& update) {
  require_active();
  output_.update_world_view(update);
}
void RenderScene::retire() noexcept {
  if (state_ == State::retired) return;
  // Synchronous backend completion already precedes this call. Clear only a
  // successfully registered scene, then release its admission for reentry.
  if (state_ == State::active) output_.clear_world_draws();
  state_ = State::retired;
  output_.scene_claimed_ = false;
}
void ActorPublisher::update_poses(std::span<const WorldPoseUpdate> updates) {
  scene_.require_active();
  scene_.output_.update_world_poses(updates);
}
void ActorPublisher::update_shapes(
    std::span<const WorldShapePositionUpdate> updates) {
  scene_.require_active();
  scene_.output_.update_shape_positions(updates);
}
void ActorPublisher::update_visibility(
    std::span<const WorldVisibilityUpdate> updates) {
  scene_.require_active();
  // Actor animation can change visibility; shadow-owner selection and opacity
  // remain scene policy. Validate the complete batch before native mutation.
  for (const auto& update : updates) {
    if (update.casts_shadow || update.owner_opacity) {
      throw std::invalid_argument(
          "Actor visibility cannot change scene policy");
    }
  }
  scene_.output_.update_world_visibility(updates);
}
}  // namespace bw::render
