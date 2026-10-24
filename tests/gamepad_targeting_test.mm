// Native GameController snapshot buttons feed the production host sampler and
// edge owner, then the game targeting owner. No connected controller, window or
// user input is modified. Explicit same-frame mode exit is distinct from raised
// animation expiry (I.1/E.4); fixed state has no per-step allocations (MEM.9).
#import <GameController/GameController.h>
#include <stdexcept>
#include "game/aim_posture.hpp"
#include "game/player_targeting.hpp"
#include "platform/macos/gamepad_actions.hpp"
void BWGamepadTargetingTests() {
  @autoreleasepool {
    GCController* controller = [GCController controllerWithExtendedGamepad];
    auto* pad = controller.extendedGamepad;
    if (pad.rightThumbstickButton == nil) {
      throw std::runtime_error("Snapshot lacks right-thumbstick button");
    }
    bw::platform::macos::GameplayButtonEdges edges;
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
