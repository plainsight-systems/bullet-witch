#ifndef BW_CHARACTERS_PLAYER_CONTROLLER_HPP
#define BW_CHARACTERS_PLAYER_CONTROLLER_HPP

#include "bw/camera/evaluator.hpp"
#include "bw/input/key_state.hpp"
#include "bw/input/player_input.hpp"
#include "bw/world/object.hpp"
#include "bw/world/tick_function.hpp"
#include "bw/world/world.hpp"

// Responsibility: the player's controller (docs/architecture/
// characters.md, invariants 1 and 2; input.md, invariant 9). It possesses
// one object, evaluates the player's input once a tick, and holds the
// control rotation, the direction the player looks and aims. It is a tick
// function the game registers in the before-simulation stage.
//
// Contract.
//   1. possess(object) ties the controller to the object and sets the
//      control rotation to the object's facing: its facing yaw
//      (bw/camera/pose.hpp, contract 5) and pitch 0. Possessing while
//      possessing another object lets the other go first. unpossess()
//      lets go; the control rotation stays. Precondition: `object` is an
//      object of the world the controller was made with.
//   2. tick(context) evaluates the player's input
//      (bw/input/player_input.hpp, contract 4), so the functions bound to
//      actions are called here, once a tick, before anything that reads
//      an action this tick.
//   3. control_rotation() is the rotation possession set until the
//      camera gives another: after each valid evaluation of a camera this
//      controller is the sink of (bw/camera/evaluator.hpp,
//      ControlRotationSink, contract 3g), the yaw and pitch of that
//      evaluation's pose.
//   4. The world, the player's input and the key state are observed and
//      outlive the controller's ticks. The controller is not
//      copied or moved once registered.
//
// Core guidelines read: C.128 (the overrides of the world's and the
// camera's interfaces), R.3 (three observers), I.1 (everything it reads is given at construction
// or set by name). Performance guidelines read: MEM.9 (a tick allocates
// nothing).
namespace bw::characters {

struct ControlRotation {
  float yaw_degrees{};
  float pitch_degrees{};
};

class PlayerController final : public world::TickFunction, public camera::ControlRotationSink {
public:
  PlayerController(const world::World& world, input::PlayerInput& input,
                   const input::KeyState& keys) noexcept;

  void possess(world::ObjectId object);
  void unpossess() noexcept;
  // kNoObject when none.
  [[nodiscard]] world::ObjectId possessed() const noexcept;

  [[nodiscard]] ControlRotation control_rotation() const noexcept;

  void tick(const world::TickContext& context) override;
  void set_control_rotation(float yaw_degrees, float pitch_degrees) override;

private:
  const world::World* world_;
  input::PlayerInput* input_;
  const input::KeyState* keys_;
  world::ObjectId possessed_{};
  ControlRotation rotation_{};
};

} // namespace bw::characters
#endif
