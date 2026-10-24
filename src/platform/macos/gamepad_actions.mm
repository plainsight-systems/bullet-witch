// Read the actual native button objects; keep edge history independent from
// menu repetition and gameplay policy. Header I.1/E.4/MEM.9 contracts apply.
#include "gamepad_actions.hpp"
#import <GameController/GameController.h>
namespace bw::platform::macos {
std::uint32_t gamepad_action_holds(GCExtendedGamepad* pad) noexcept {
  return (pad.buttonX.pressed ? kReloadButton : 0U) | (pad.buttonA.pressed ? kAttackButton : 0U) |
         (pad.buttonB.pressed ? kWeaponButton : 0U) |
         (pad.rightThumbstickButton.pressed ? kTargetButton : 0U);
}
}  // namespace bw::platform::macos
