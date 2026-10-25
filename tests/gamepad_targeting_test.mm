// Native GameController snapshots exercise physical button sampling and edge
// rearming for the added buttons; existing native-to-targeting regression
// coverage remains below. New selection/chord policy is tested by game owners.
// No connected controller, window or user input is modified. Both-phase Core
// I.1 (#ri-explicit) keeps observed buttons independent from policy; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps edge state fixed.
#import <GameController/GameController.h>
#include <stdexcept>
#include "game/aim_posture.hpp"
#include "game/player_targeting.hpp"
#include "platform/macos/gamepad_actions.hpp"
void BWGamepadTargetingTests() {
  @autoreleasepool {
    namespace input = bw::platform::macos;
    GCController* controller = [GCController controllerWithExtendedGamepad];
    auto* pad = controller.extendedGamepad;
    if (pad.rightThumbstickButton == nil) {
      throw std::runtime_error("Snapshot lacks right-thumbstick button");
    }
    input::GameplayButtonEdges edges;
    const auto verify_button = [&](GCControllerButtonInput* button, std::uint32_t bit) {
      [button setValue:1];
      const auto held = input::gamepad_action_holds(pad);
      if (held != bit || edges.sample(held) != bit || edges.sample(held) != 0) {
        throw std::runtime_error("Native button press/hold observation mismatch");
      }
      [button setValue:0];
      if (edges.sample(input::gamepad_action_holds(pad)) != 0) {
        throw std::runtime_error("Native release generated a press");
      }
      [button setValue:1];
      if (edges.sample(input::gamepad_action_holds(pad)) != bit) {
        throw std::runtime_error("Native release did not rearm the button edge");
      }
      [button setValue:0];
      (void)edges.sample(input::gamepad_action_holds(pad));
    };
    verify_button(pad.leftShoulder, input::kShoulderLeftButton);
    verify_button(pad.rightShoulder, input::kShoulderRightButton);
    verify_button(pad.buttonY, input::kFaceNorthButton);
    verify_button(pad.buttonB, input::kFaceEastButton);
    verify_button(pad.buttonA, input::kFaceSouthButton);
    verify_button(pad.buttonX, input::kFaceWestButton);
    verify_button(pad.rightThumbstickButton, input::kTargetButton);
    // Simultaneous observations preserve both positions without choosing a
    // gesture, prioritizing a face button or consuming the opposite edge.
    [pad.rightShoulder setValue:1];
    (void)edges.sample(input::gamepad_action_holds(pad));
    [pad.leftShoulder setValue:1];
    const auto shoulders = input::kShoulderLeftButton | input::kShoulderRightButton;
    const auto held = input::gamepad_action_holds(pad);
    if (held != shoulders || edges.sample(held) != input::kShoulderLeftButton ||
        edges.sample(held) != 0) {
      throw std::runtime_error("Shoulder holds and press edges were conflated");
    }
    [pad.leftShoulder setValue:0];
    [pad.rightShoulder setValue:0];
    (void)edges.sample(input::gamepad_action_holds(pad));
    [pad.buttonY setValue:1];
    [pad.buttonB setValue:1];
    if (edges.sample(input::gamepad_action_holds(pad)) !=
        (input::kFaceNorthButton | input::kFaceEastButton)) {
      throw std::runtime_error("Simultaneous face edges were collapsed");
    }
    // Keyboard taps may be explicit edges even when key-up was drained in the
    // same pump. The next empty sample does not repeat the tap.
    (void)edges.sample(0);
    if (edges.sample(0, input::kShoulderLeftButton) != input::kShoulderLeftButton ||
        edges.sample(0) != 0) {
      throw std::runtime_error("Explicit physical tap was lost or repeated");
    }
    [pad.buttonY setValue:0];
    [pad.buttonB setValue:0];
    (void)edges.sample(input::gamepad_action_holds(pad));
    bw::game::PlayerTargeting targeting;
    bw::game::RaisedWeaponLifetime posture;
    constexpr float kStep = 1.0F / 60;
    auto tick = [&] {
      const auto press = edges.sample(bw::platform::macos::gamepad_action_holds(pad));
      targeting.advance(kStep, (press & bw::platform::macos::kTargetButton) != 0, false, false);
      (void)posture.advance(kStep, false, targeting.active(), 0);
      return press;
    };
    [pad.rightThumbstickButton setValue:1];
    if (tick() == 0 || !targeting.selected() || !targeting.camera_targeted()) {
      throw std::runtime_error("First native RS press did not enter targeting");
    }
    if (tick() != 0 || !targeting.selected()) {
      throw std::runtime_error("Held native RS generated another mode press");
    }
    [pad.rightThumbstickButton setValue:0];
    if (tick() != 0) throw std::runtime_error("Release generated a press");
    [pad.rightThumbstickButton setValue:1];
    if (tick() == 0 || targeting.selected() || targeting.active()) {
      throw std::runtime_error("Second native RS press did not leave targeting");
    }
    [pad.rightThumbstickButton setValue:0];
    (void)tick();
    if (targeting.camera_targeted() || !posture.active()) {
      throw std::runtime_error("Camera exit was conflated with weapon posture");
    }
    // RT alone can retain posture while ordinary targeting mode stays selected.
    targeting.advance(kStep, false, true, false);
    if (!targeting.active() || targeting.selected() || targeting.camera_targeted()) {
      throw std::runtime_error("Fire intent incorrectly selected zoom targeting");
    }
  }
}
