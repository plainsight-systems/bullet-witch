// Unit test: the player's controller (bw/characters/player_controller.hpp),
// one table-driven scenario.
#include "bw/characters/player_controller.hpp"

#include "bw/camera/evaluator.hpp"
#include "bw/camera/rig.hpp"
#include "bw/input/key_state.hpp"
#include "bw/input/player_input.hpp"
#include "bw/input/triggers.hpp"
#include "bw/math/rotation/quaternion.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/platform/input/device_input.hpp"
#include "bw/testing/harness.hpp"
#include "bw/world/object.hpp"
#include "bw/world/tick_function.hpp"
#include "bw/world/world.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <span>

namespace {

using bw::testing::require;

class FirstRig final : public bw::camera::Director {
public:
  bw::camera::RigId rig{};
  std::size_t run(double /*step*/, std::span<bw::camera::RigRequest> out) override {
    out[0] = bw::camera::RigRequest{rig, bw::camera::Layer::Main, nullptr};
    return 1;
  }
};

void count_call(void* user, const bw::input::ActionState& /*state*/) { ++*static_cast<int*>(user); }

// (player_controller.hpp, contracts 1-3): possession takes the facing yaw
// of an object turned a quarter to its right; each world tick evaluates the
// input once, so a function bound to a held action is called once a tick;
// after each valid evaluation of the camera it is the sink of, the control
// rotation is that pose's.
void player_controller_table() {
  constexpr std::array kActions{bw::input::ActionDescription{"Hold"},
                                bw::input::ActionDescription{"Look", bw::input::ValueType::Axis2D}};
  static constexpr std::array kDown{bw::input::Trigger{bw::input::TriggerKind::Down, 0.5F}};
  static constexpr std::array kMappings{bw::input::MappingDescription{"Hold", "KeyW", {}, kDown}};
  const auto world = bw::world::World::create({});
  bw::world::Transform turned{};
  turned.rotation = bw::math::Quaternion4f::from_axis_angle(bw::math::Vector3f{0.0F, 1.0F, 0.0F},
                                                            -std::numbers::pi_v<float> / 2.0F);
  const bw::world::ObjectId object = world->create_object(turned).id;
  const auto input = bw::input::PlayerInput::create({});
  require(input->load_actions(kActions).ok(), "the actions load");
  const auto context = input->load_context({"hold", kMappings});
  require(context.ok() && input->add_context(context.id, 0) == bw::input::InputError::None,
          "the context applies");
  int calls = 0;
  require(input->bind(input->find_action("Hold"), bw::input::ActionEvent::Fired, &count_call,
                      &calls) == bw::input::InputError::None,
          "the binding is made");
  bw::input::KeyState keys;
  bw::characters::PlayerController controller(*world, *input, keys);
  require(
      world->register_tick_function("controller", bw::world::Stage::BeforeSimulation, controller)
          .ok(),
      "the controller registers");
  controller.possess(object);
  require(controller.possessed() == object, "the object is possessed");
  require(std::abs(controller.control_rotation().yaw_degrees - 90.0F) < 1.0e-3F &&
              controller.control_rotation().pitch_degrees == 0.0F,
          "possession sets the object's facing yaw and pitch 0");

  const std::array nodes{bw::camera::boom({}, bw::camera::InputSlot{"Look"})};
  const std::array rigs{bw::camera::RigDescription{"rig", nodes, {}, {}}};
  const auto evaluator = bw::camera::Evaluator::create({});
  require(evaluator->load_rigs(rigs, *input).ok(), "the rig loads");
  FirstRig director;
  director.rig = evaluator->find_rig("rig");
  evaluator->set_director(&director);
  evaluator->set_control_rotation_sink(&controller);
  require(world->set_camera(*evaluator, object) == bw::world::WorldError::None, "camera set");
  require(std::abs(controller.control_rotation().yaw_degrees - 90.0F) < 1.0e-3F,
          "until the camera gives one the rotation is possession's");

  keys.accumulate({bw::platform::InputElement::KeyW, 1.0F});
  for (int tick = 1; tick <= 3; ++tick) {
    keys.begin_frame();
    static_cast<void>(world->tick(1.0 / 60.0));
    require(calls == tick, static_cast<std::size_t>(tick), "one evaluation per tick");
  }
  // The rig's slot starts at 0, so its pose's yaw differs from the facing.
  require(controller.control_rotation().yaw_degrees == evaluator->evaluation().pose.yaw_degrees &&
              controller.control_rotation().pitch_degrees ==
                  evaluator->evaluation().pose.pitch_degrees &&
              controller.control_rotation().yaw_degrees == 0.0F,
          "the control rotation is the camera's");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"player_controller_table", &player_controller_table},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
