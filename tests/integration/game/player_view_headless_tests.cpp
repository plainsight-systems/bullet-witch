// Integration test: the player's view composed with no window and no game
// data: the world (bw/world/world.hpp), key state, the player's input with
// the game's tables, the controller, the evaluator and Alicia's view
// (bw/game/characters/alicia/view.hpp), driven by the scripted device's
// samples at a fixed step, read through the world's publication.
#include "bw/camera/evaluator.hpp"
#include "bw/camera/pose.hpp"
#include "bw/characters/player_controller.hpp"
#include "bw/game/characters/alicia/view.hpp"
#include "bw/game/input/actions.hpp"
#include "bw/input/key_state.hpp"
#include "bw/input/player_input.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/platform/input/scripted_device.hpp"
#include "bw/testing/harness.hpp"
#include "bw/world/object.hpp"
#include "bw/world/tick_function.hpp"
#include "bw/world/world.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace {

namespace alicia = bw::game::alicia;
using bw::testing::require;

// The composition game_host makes with --asset, without the renderer
// (world.md, invariant 16).
class Composition {
public:
  Composition()
      : world_(bw::world::World::create({})), input_(bw::input::PlayerInput::create({})),
        evaluator_(bw::camera::Evaluator::create({})),
        controller_(std::make_unique<bw::characters::PlayerController>(*world_, *input_, keys_)) {
    // Her object at her place, kBodyBelowPlace above her feet on the origin.
    bw::world::Transform place{};
    place.location = bw::math::Vector3f{0.0F, alicia::kBodyBelowPlace, 0.0F};
    const bw::world::ObjectResult object = world_->create_object(place);
    require(object.ok(), "the object is made");
    require(bw::game::load_game_input(*input_).ok(), "the game's input loads");
    require(world_
                ->register_tick_function("player controller", bw::world::Stage::BeforeSimulation,
                                         *controller_)
                .ok(),
            "the controller registers");
    controller_->possess(object.id);
    require(view_.setup(*input_, *controller_, *evaluator_).ok(), "the view sets up");
    require(world_->set_camera(*evaluator_, object.id) == bw::world::WorldError::None,
            "the camera is set");
  }

  // Ticks 0 to the script's last at `step`, the samples of each tick handed
  // to key state before it; `at_mark(name, tick)` after each marked tick.
  template <class AtMark> void run(std::string_view text, double step, AtMark at_mark) {
    const bw::platform::InputScript script = bw::platform::parse_script(text);
    require(script.ok(), "the script parses: " + std::string(text));
    for (std::uint32_t tick = 0; tick <= bw::platform::last_tick(script); ++tick) {
      for (const bw::platform::ScriptCommand& command : bw::platform::commands_at(script, tick)) {
        if (command.kind == bw::platform::ScriptCommandKind::Set) {
          keys_.accumulate(command.sample);
        }
      }
      keys_.begin_frame();
      static_cast<void>(world_->tick(step));
      for (const bw::platform::ScriptCommand& command : bw::platform::commands_at(script, tick)) {
        if (command.kind == bw::platform::ScriptCommandKind::Mark) {
          at_mark(std::string_view(command.name), tick);
        }
      }
    }
  }

  [[nodiscard]] bw::world::World& world() noexcept { return *world_; }
  [[nodiscard]] const bw::camera::Evaluator& evaluator() const noexcept { return *evaluator_; }

private:
  std::unique_ptr<bw::world::World> world_;
  bw::input::KeyState keys_{};
  std::unique_ptr<bw::input::PlayerInput> input_;
  std::unique_ptr<bw::camera::Evaluator> evaluator_;
  std::unique_ptr<bw::characters::PlayerController> controller_;
  alicia::PlayerView view_{};
};

constexpr double kStep = 1.0 / 60.0;
constexpr double kViewChangeStep = 1.0 / 64.0;
constexpr float kDegrees = 1.0e-3F;

void near(float observed, float expected, float tolerance, const std::string& what) {
  require(std::abs(observed - expected) <= tolerance,
          what + ": " + std::to_string(observed) + ", want " + std::to_string(expected));
}

std::string rigs_of(const Composition& composition) {
  std::string text;
  for (const bw::camera::StackEntry& entry : composition.evaluator().stack()) {
    text += std::string(composition.evaluator().rig_name(entry.rig)) + ":" +
            std::to_string(entry.weight) + " ";
  }
  return text;
}

// (alicia/view.hpp, contracts 1, 2 and 4; world.md, invariants 9, 10, 14
// and 16): the standard view on the first tick, from the rig's values above
// her place, with the cut flag set until cleared; a full stick to the left
// turns 100 * 1.7 degrees a second, the yaw wrapped into [0, 360); 0.2 is inside the dead zone; 246
// mouse counts turn 10 degrees at 1/60; the pitch holds at the limit; the view action blends to the
// shooting view's field and rate over the view change; pressed again
// mid-blend it puts a third rig on the stack.
void player_view_headless() {
  {
    Composition composition;
    composition.run(
        "0 mark first\n1 mark second\n1 end", kStep,
        [&](std::string_view mark, std::uint32_t /*tick*/) {
          const bw::world::Publication published = composition.world().publication();
          require(published.camera_valid, "the camera is published");
          require(published.camera_cut, "the cut flag is set and stays set: " + std::string(mark));
          if (mark != "first") {
            return;
          }
          const bw::math::Vector3f expected =
              bw::math::Vector3f{0.0F, alicia::kBodyBelowPlace + alicia::kPivotHeight, 0.0F} +
              bw::camera::right(0.0F) * alicia::kShoulder +
              bw::camera::forward(0.0F, 0.0F) * -alicia::kBehind;
          near(published.camera.location.x, expected.x, 1.0e-5F, "x");
          near(published.camera.location.y, expected.y, 1.0e-5F, "y");
          near(published.camera.location.z, expected.z, 1.0e-5F, "z");
          near(published.camera.yaw_degrees, 0.0F, 0.0F, "yaw");
          near(published.camera.pitch_degrees, 0.0F, 0.0F, "pitch");
          near(bw::camera::vertical_fov_degrees(published.camera), alicia::kStandardFov, 1.0e-3F,
               "vertical field");
        });
    composition.world().clear_camera_cut();
    require(!composition.world().publication().camera_cut, "clear_camera_cut clears it");
    composition.run("0 end", kStep, [](std::string_view, std::uint32_t) {});
    require(!composition.world().publication().camera_cut, "no later tick cuts");
  }
  // (script, step, mark, yaw, pitch) for the turns.
  struct Turn {
    std::string_view script;
    std::string_view mark;
    float yaw;
    float pitch;
  };
  constexpr std::array turns{
      Turn{"0 set PadRightX -1\n29 mark turn\n29 end", "turn",
           360.0F - alicia::kTurnRateStandard * bw::game::kLookSensitivity * 0.5F, 0.0F},
      Turn{"0 set PadRightX 0.2\n59 mark still\n59 end", "still", 0.0F, 0.0F},
      Turn{"0 set MouseMotionX 246\n0 mark mouse\n0 end", "mouse", 10.0F, 0.0F},
      Turn{"0 set PadRightY 1\n119 mark up\n119 end", "up", 0.0F, alicia::kPitchLimit},
      Turn{"0 set PadRightY 1\n120 set PadRightY -1\n359 mark down\n359 end", "down", 0.0F,
           -alicia::kPitchLimit},
  };
  for (const Turn& turn : turns) {
    Composition composition;
    bool marked = false;
    composition.run(turn.script, kStep, [&](std::string_view mark, std::uint32_t) {
      const bw::camera::Pose& pose = composition.world().publication().camera;
      near(pose.yaw_degrees, turn.yaw, kDegrees, std::string(mark) + " yaw");
      near(pose.pitch_degrees, turn.pitch, kDegrees, std::string(mark) + " pitch");
      marked = mark == turn.mark;
    });
    require(marked, "the mark " + std::string(turn.mark) + " was reached");
  }
  {
    // At 1/64 the view change's 0.25 s is 16 ticks: the press before tick
    // 1, the blend full at tick 16; then 64 ticks of full stick are 1 s.
    Composition composition;
    float yaw_after = 0.0F;
    composition.run(
        "1 set KeyE 1\n2 set KeyE 0\n16 mark changed\n17 set PadRightX 1\n80 mark turned\n80 end",
        kViewChangeStep, [&](std::string_view mark, std::uint32_t) {
          const bw::camera::Pose& pose = composition.world().publication().camera;
          if (mark == "changed") {
            require(composition.evaluator().stack().size() == 1U &&
                        composition.evaluator().rig_name(composition.evaluator().stack()[0].rig) ==
                            alicia::kRigShooting,
                    "one rig, the shooting view's: " + rigs_of(composition));
            near(bw::camera::vertical_fov_degrees(pose), alicia::kShootingFov, 1.0e-3F,
                 "the shooting view's vertical field");
            yaw_after = pose.yaw_degrees;
            return;
          }
          near(pose.yaw_degrees - yaw_after, alicia::kTurnRateShooting * bw::game::kLookSensitivity,
               1.0e-2F, "the shooting view's turn in one second");
        });
  }
  {
    // Pressed again 0.1 s into the blend: a third entry.
    Composition composition;
    composition.run(
        "1 set KeyE 1\n2 set KeyE 0\n7 set KeyE 1\n7 mark again\n7 end", kStep,
        [&](std::string_view, std::uint32_t) {
          const auto stack = composition.evaluator().stack();
          require(stack.size() == 3U &&
                      composition.evaluator().rig_name(stack[0].rig) == alicia::kRigStandard &&
                      composition.evaluator().rig_name(stack[1].rig) == alicia::kRigShooting &&
                      composition.evaluator().rig_name(stack[2].rig) == alicia::kRigStandard,
                  "three rigs on the stack: " + rigs_of(composition));
        });
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"player_view_headless", &player_view_headless},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
