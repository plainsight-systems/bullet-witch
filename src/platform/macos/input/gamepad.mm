// Read the actual native button objects; keep edge history independent from
// menu repetition and gameplay policy. Both-phase Core I.1 (#ri-explicit)
// publishes physical positions without interpreting gestures; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps sampling fixed.
#include "gamepad.hpp"
#import <GameController/GameController.h>
#include <array>
namespace bw::platform::macos {
std::uint16_t gamepad_physical_buttons(GCExtendedGamepad* pad) noexcept {
  // The source digital trigger threshold is one half; analog values are
  // published separately so binding a fire action retains trigger magnitude.
  constexpr float kTriggerThreshold = .5F;
  const std::array<bool, 16> buttons{static_cast<bool>(pad.dpad.up.pressed),
                                     static_cast<bool>(pad.dpad.down.pressed),
                                     static_cast<bool>(pad.dpad.left.pressed),
                                     static_cast<bool>(pad.dpad.right.pressed),
                                     static_cast<bool>(pad.buttonMenu.pressed),
                                     static_cast<bool>(pad.buttonOptions.pressed),
                                     static_cast<bool>(pad.leftThumbstickButton.pressed),
                                     static_cast<bool>(pad.rightThumbstickButton.pressed),
                                     static_cast<bool>(pad.leftShoulder.pressed),
                                     static_cast<bool>(pad.rightShoulder.pressed),
                                     pad.leftTrigger.value > kTriggerThreshold,
                                     pad.rightTrigger.value > kTriggerThreshold,
                                     static_cast<bool>(pad.buttonA.pressed),
                                     static_cast<bool>(pad.buttonB.pressed),
                                     static_cast<bool>(pad.buttonX.pressed),
                                     static_cast<bool>(pad.buttonY.pressed)};
  std::uint16_t held{};
  std::uint16_t bit = 1;
  for (const bool down : buttons) {
    if (down) held |= bit;
    bit = static_cast<std::uint16_t>(bit << 1U);
  }
  return held;
}
}  // namespace bw::platform::macos
