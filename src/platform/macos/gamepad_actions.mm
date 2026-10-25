// Read the actual native button objects; keep edge history independent from
// menu repetition and gameplay policy. Both-phase Core I.1 (#ri-explicit)
// publishes physical positions without interpreting gestures; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps sampling fixed.
#include "gamepad_actions.hpp"
#import <GameController/GameController.h>
namespace bw::platform::macos {
std::uint32_t gamepad_action_holds(GCExtendedGamepad* pad) noexcept {
  return (pad.buttonX.pressed ? kReloadButton : 0U) | (pad.buttonA.pressed ? kAttackButton : 0U) |
         (pad.buttonB.pressed ? kWeaponButton : 0U) |
         (pad.rightThumbstickButton.pressed ? kTargetButton : 0U) |
         (pad.leftShoulder.pressed ? kShoulderLeftButton : 0U) |
         (pad.rightShoulder.pressed ? kShoulderRightButton : 0U) |
         (pad.buttonY.pressed ? kFaceNorthButton : 0U);
}
}  // namespace bw::platform::macos
