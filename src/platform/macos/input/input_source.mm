// Device event accumulation and polling, preserving the accepted native order.
// WindowHost filters fullscreen shortcuts and owns dispatch; this owner converts
// remaining events to neutral positions, polls axes/buttons, then clears input
// on focus loss before deriving controller edges. Keyboard taps retain explicit
// edges even after their release. No repeat/action policy or per-sample heap
// storage. Both-phase Core I.1 (#ri-explicit), C.9 (#rc-private), performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) apply to the bounded
// private history. See neutral-input-boundary.md for source/reference contracts.
#include "input_source.hpp"
#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include <algorithm>
#include <cmath>
#include "physical_keys.hpp"
namespace bw::platform::macos {
void InputSource::begin() noexcept {
  physical_.keys_pressed.reset();
  physical_.pad_held = 0;
  physical_.mouse_pressed = 0;
  physical_.mouse_delta = {};
  physical_.wheel = 0;
  physical_.movement = {};
  physical_.look = {};
  physical_.movement_minimum = {};
  physical_.movement_maximum = {};
  physical_.left_trigger = physical_.right_trigger = 0;
}
void InputSource::observe(NSEvent* event) {
  auto& physical = physical_;
  if (event.type == NSEventTypeKeyDown || event.type == NSEventTypeKeyUp) {
    const auto code = event.keyCode;
    const auto usage = keyboard_usage(code);
    if (usage != 0) {
      const bool down = event.type == NSEventTypeKeyDown;
      if (down && !event.isARepeat) physical.keys_pressed.set(usage);
      physical.keys_held.set(usage, down);
    }
  } else {
    if (event.type == NSEventTypeFlagsChanged) {
      // Device-specific modifier bits retain left/right key identity even
      // while both keys are held. The independent mask collapses that state.
      constexpr std::array<std::pair<NSEventModifierFlags, std::uint16_t>, 8> kModifiers{
          {{NX_DEVICELSHIFTKEYMASK, 225},
           {NX_DEVICERSHIFTKEYMASK, 229},
           {NX_DEVICELCTLKEYMASK, 224},
           {NX_DEVICERCTLKEYMASK, 228},
           {NX_DEVICELALTKEYMASK, 226},
           {NX_DEVICERALTKEYMASK, 230},
           {NX_DEVICELCMDKEYMASK, 227},
           {NX_DEVICERCMDKEYMASK, 231}}};
      for (const auto& [flag, usage] : kModifiers) {
        const bool down = (event.modifierFlags & flag) != 0;
        if (down && !physical.keys_held.test(usage)) physical.keys_pressed.set(usage);
        physical.keys_held.set(usage, down);
      }
    }
    if (event.type == NSEventTypeMouseMoved || event.type == NSEventTypeLeftMouseDragged ||
        event.type == NSEventTypeRightMouseDragged || event.type == NSEventTypeOtherMouseDragged) {
      physical.mouse_delta[0] += static_cast<float>(event.deltaX);
      physical.mouse_delta[1] -= static_cast<float>(event.deltaY);
    }
    if (event.type == NSEventTypeScrollWheel) {
      physical.wheel += event.scrollingDeltaY > 0 ? 1 : (event.scrollingDeltaY < 0 ? -1 : 0);
    }
    const bool mouse_down = event.type == NSEventTypeLeftMouseDown ||
                            event.type == NSEventTypeRightMouseDown ||
                            event.type == NSEventTypeOtherMouseDown;
    const bool mouse_up = event.type == NSEventTypeLeftMouseUp ||
                          event.type == NSEventTypeRightMouseUp ||
                          event.type == NSEventTypeOtherMouseUp;
    constexpr NSInteger kMouseButtonCount = 8;
    if ((mouse_down || mouse_up) && event.buttonNumber >= 0 &&
        event.buttonNumber < kMouseButtonCount) {
      const auto bit = static_cast<std::uint8_t>(1U << event.buttonNumber);
      if (mouse_down) {
        physical.mouse_pressed |= bit;
        physical.mouse_held |= bit;
      } else {
        physical.mouse_held &= static_cast<std::uint8_t>(~bit);
      }
    }
  }
}
input::PhysicalState InputSource::finish(bool focused) {
  auto& physical = physical_;
  NSArray<GCController*>* controllers = [GCController controllers];
  bool controller_connected = false;
  for (NSUInteger index = 0; index < controllers.count; ++index) {
    controller_connected = controller_connected || controllers[index].extendedGamepad != nil;
  }
  if (focused) {
    for (NSUInteger index = 0; index < controllers.count; ++index) {
      GCController* controller = controllers[index];
      GCExtendedGamepad* pad = controller.extendedGamepad;
      if (pad == nil) continue;
      const auto merge_axis = [](float keyboard, float controller_axis) {
        return std::abs(controller_axis) > std::abs(keyboard) ? controller_axis : keyboard;
      };
      physical.movement[0] = merge_axis(physical.movement[0], pad.leftThumbstick.xAxis.value);
      physical.movement[1] = merge_axis(physical.movement[1], pad.leftThumbstick.yAxis.value);
      physical.look[0] = merge_axis(physical.look[0], pad.rightThumbstick.xAxis.value);
      physical.look[1] = merge_axis(physical.look[1], pad.rightThumbstick.yAxis.value);
      physical.left_trigger = std::max(physical.left_trigger, pad.leftTrigger.value);
      physical.right_trigger = std::max(physical.right_trigger, pad.rightTrigger.value);
      for (std::size_t axis = 0; axis < 2; ++axis) {
        const auto value =
            axis == 0 ? pad.leftThumbstick.xAxis.value : pad.leftThumbstick.yAxis.value;
        physical.movement_minimum.at(axis) = std::min(physical.movement_minimum.at(axis), value);
        physical.movement_maximum.at(axis) = std::max(physical.movement_maximum.at(axis), value);
      }
      physical.pad_held |= gamepad_physical_buttons(pad);
    }
  } else {
    // Retire held state instead of carrying it through reactivation.
    physical = {};
  }
  physical.controller_connected = controller_connected;
  physical.pad_pressed = pad_edges_.sample(physical.pad_held);
  return physical;
}
}  // namespace bw::platform::macos
