// The player's controller (bw/characters/player_controller.hpp).
#include "bw/characters/player_controller.hpp"

#include "bw/camera/evaluator.hpp"
#include "bw/camera/pose.hpp"
#include "bw/input/key_state.hpp"
#include "bw/input/player_input.hpp"
#include "bw/world/object.hpp"
#include "bw/world/tick_function.hpp"
#include "bw/world/world.hpp"

namespace bw::characters {

PlayerController::PlayerController(const world::World& world, input::PlayerInput& input,
                                   const input::KeyState& keys) noexcept
    : world_(&world), input_(&input), keys_(&keys) {}

void PlayerController::possess(world::ObjectId object) {
  if (possessed_ != world::kNoObject) {
    unpossess();
  }
  possessed_ = object;
  // A character stands upright, so the control rotation starts level.
  rotation_ = ControlRotation{camera::facing_yaw_degrees(world_->transform(object).rotation), 0.0F};
}

void PlayerController::unpossess() noexcept { possessed_ = world::kNoObject; }

world::ObjectId PlayerController::possessed() const noexcept { return possessed_; }

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void PlayerController::set_control_rotation(float yaw_degrees, float pitch_degrees) {
  rotation_ = ControlRotation{yaw_degrees, pitch_degrees};
}

ControlRotation PlayerController::control_rotation() const noexcept { return rotation_; }

void PlayerController::tick(const world::TickContext& /*context*/) { input_->evaluate(*keys_); }

} // namespace bw::characters
