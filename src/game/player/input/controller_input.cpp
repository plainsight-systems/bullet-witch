// Active prompts and fixed UI actions share the immutable binding descriptor.
// Input activity updates one owner; connection only admits controller use.
// Both-phase I.1/MEM.9 keep that family a bounded copied observation.
// Source keyboard scan positions are translated from USB observations, then
// duplicate-free saved bindings select held/pressed game intentions. Controller
// compatibility applies the original device-map fallback. Mouse look remains
// a separate delta, so camera settings do not clamp it into a stick range.
// Original mapping arrays/history are bounded; no per-frame allocation. Both
// phases read Core I.1 (#ri-explicit), C.9 (#rc-private), and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md): neutral input and
// supplied time feed one private repeat owner. See neutral-input-boundary.md.
#include "controller_input.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bw::game {
namespace {
// IBM/DirectInput physical positions -> USB HID Keyboard usages. Zero denotes
// a position absent on the native keyboard; mouse pseudo-codes are separate.
constexpr std::array<std::uint16_t, 128> kScanUsages{
    0,  41, 30, 31, 32, 33, 34,  35, 36,  37, 38,  39, 45, 46,  42, 43,
    20, 26, 8,  21, 23, 28, 24,  12, 18,  19, 47,  48, 40, 224, 4,  22,
    7,  9,  10, 11, 13, 14, 15,  51, 52,  53, 225, 49, 29, 27,  6,  25,
    5,  17, 16, 54, 55, 56, 229, 85, 226, 44, 57,  58, 59, 60,  61, 62,
    63, 64, 65, 66, 67, 83, 71,  95, 96,  97, 86,  92, 93, 94,  87, 89,
    90, 91, 98, 99, 0,  0,  100, 68, 69,  0,  0,   0,  0,  0,   0,  0,
    0,  0,  0,  0,  0,  0,  0,   0,  0,   0,  0,   0,  0,  0,   0,  0,
    0,  0,  0,  0,  0,  0,  0,   0,  0,   0,  0,   0,  0,  0,   0,  0};
constexpr std::array<std::pair<std::uint16_t, std::uint16_t>, 17> kExtended{
    {{0x9c, 88},
     {0x9d, 228},
     {0xb5, 84},
     {0xb7, 70},
     {0xb8, 230},
     {0xc7, 74},
     {0xc8, 82},
     {0xc9, 75},
     {0xcb, 80},
     {0xcd, 79},
     {0xcf, 77},
     {0xd0, 81},
     {0xd1, 78},
     {0xd2, 73},
     {0xd3, 76},
     {0xdb, 227},
     {0xdc, 231}}};
constexpr std::uint16_t kMouseFirst = 0x12c, kMouseEnd = 0x134,
                        kWheelUp = 0x134, kWheelDown = 0x135;
bool key_state(const input::PhysicalState& input, std::uint16_t code,
               bool pressed) {
  if (code >= kMouseFirst && code < kMouseEnd) {
    return ((pressed ? input.mouse_pressed : input.mouse_held) &
            (1U << (code - kMouseFirst))) != 0;
  }
  if (code == kWheelUp) return input.wheel > 0;
  if (code == kWheelDown) return input.wheel < 0;
  const auto usage = source_key_to_usb(code);
  return usage != 0 &&
         (pressed ? input.keys_pressed : input.keys_held).test(usage);
}
float merge_axis(float lhs, float rhs) {
  return std::abs(lhs) > std::abs(rhs) ? lhs : rhs;
}
}  // namespace
std::uint16_t source_key_to_usb(std::uint16_t code) {
  if (code < kScanUsages.size()) return kScanUsages.at(code);
  const auto found = std::ranges::find_if(
      kExtended, [code](const auto& item) { return item.first == code; });
  return found == kExtended.end() ? 0 : found->second;
}
std::optional<std::uint16_t> captured_keyboard_binding(
    const input::PhysicalState& input) {
  for (std::uint16_t code = 1; code <= kWheelDown; ++code) {
    if (key_state(input, code, true)) return code;
  }
  return std::nullopt;
}
std::optional<std::uint8_t> captured_controller_binding(
    const input::PhysicalState& input) {
  for (std::uint8_t code = 0; code < kControllerButtonCount; ++code) {
    if ((input.pad_pressed & (1U << code)) != 0) return code;
  }
  return std::nullopt;
}
MappedInput ControllerInput::map(const input::InputFrame& sample,
                                 const ControllerSettings& settings,
                                 DeviceOptionsSnapshot device,
                                 float sampling_seconds,
                                 float presentation_seconds) {
  if (!std::isfinite(sampling_seconds) || sampling_seconds < 0 ||
      !std::isfinite(presentation_seconds) || presentation_seconds < 0) {
    throw std::invalid_argument("Invalid input sampling interval");
  }
  MappedInput result;
  result.physical = sample.physical;
  result.close_requested = sample.close_requested;
  result.controller_connected = sample.physical.controller_connected;
  const auto& physical = sample.physical;
  // Fixed UI holds and keyboard taps are independent of saved player bindings.
  // Keep merged holds distinct from explicit key-down edges: a second key
  // mapped to the same UI action still reports its tap, and down/up in one pump
  // survives.
  constexpr std::size_t kEnterKey = 40, kEscapeKey = 41, kUpKey = 82,
                        kDownKey = 81, kLeftKey = 80, kRightKey = 79;
  constexpr std::uint32_t kConfirmAction = 0x1000, kCancelAction = 0x2000,
                          kStartAction = 0x10, kUpAction = 0x01000000,
                          kDownAction = 0x02000000, kLeftAction = 0x04000000,
                          kRightAction = 0x08000000;
  const auto keyboard_ui = [](const auto& keys) {
    const auto bound = [&](std::size_t slot) {
      return keys.test(
          source_key_to_usb(kOriginalMenuBindings.navigation_keys.at(slot)));
    };
    return ((keys.test(kEnterKey) ||
             keys.test(
                 source_key_to_usb(kOriginalMenuBindings.confirm_keys[1])))
                ? kConfirmAction
                : 0U) |
           (keys.test(kEscapeKey) ? kCancelAction : 0U) |
           ((keys.test(kUpKey) || bound(0)) ? kUpAction : 0U) |
           ((keys.test(kDownKey) || bound(1)) ? kDownAction : 0U) |
           ((keys.test(kLeftKey) || bound(2)) ? kLeftAction : 0U) |
           ((keys.test(kRightKey) || bound(3)) ? kRightAction : 0U);
  };
  const auto held_button = [&](input::PadButton button) {
    return (physical.pad_held & input::pad_bit(button)) != 0;
  };
  constexpr float kNavigationThreshold = .5F;
  const auto axis_minimum = [&](std::size_t axis) {
    return std::min(physical.movement.at(axis),
                    physical.movement_minimum.at(axis));
  };
  const auto axis_maximum = [&](std::size_t axis) {
    return std::max(physical.movement.at(axis),
                    physical.movement_maximum.at(axis));
  };
  const auto pad_ui =
      (held_button(kOriginalMenuBindings.confirm_button) ? kConfirmAction
                                                         : 0U) |
      (held_button(kOriginalMenuBindings.back_button) ? kCancelAction : 0U) |
      (held_button(input::PadButton::menu) ? kStartAction : 0U) |
      ((held_button(input::PadButton::up) ||
        axis_maximum(1) > kNavigationThreshold)
           ? kUpAction
           : 0U) |
      ((held_button(input::PadButton::down) ||
        axis_minimum(1) < -kNavigationThreshold)
           ? kDownAction
           : 0U) |
      ((held_button(input::PadButton::left) ||
        axis_minimum(0) < -kNavigationThreshold)
           ? kLeftAction
           : 0U) |
      ((held_button(input::PadButton::right) ||
        axis_maximum(0) > kNavigationThreshold)
           ? kRightAction
           : 0U);
  const auto held = keyboard_ui(physical.keys_held) | pad_ui;
  const bool keyboard_mouse_activity =
      physical.keys_pressed.any() || physical.mouse_pressed != 0 ||
      physical.wheel != 0 || physical.mouse_delta[0] != 0 ||
      physical.mouse_delta[1] != 0;
  const bool pad_activity =
      physical.pad_pressed != 0 || (pad_ui & ~previous_pad_ui_held_) != 0;
  if (!physical.controller_connected || keyboard_mouse_activity) {
    prompt_device_ = MenuInputDevice::keyboard_mouse;
  } else if (pad_activity) {
    prompt_device_ = MenuInputDevice::controller;
  }
  previous_pad_ui_held_ = pad_ui;
  result.prompt_device = prompt_device_;
  // Preserve controller taps released within the same native pump, just as
  // keyboard edges survive independently of the final held observation.
  const auto pad_tap = [&](input::PadButton button, std::uint32_t action) {
    return (physical.pad_pressed & input::pad_bit(button)) != 0 ? action : 0U;
  };
  result.pressed_actions =
      keyboard_ui(physical.keys_pressed) | (held & ~previous_ui_held_) |
      pad_tap(kOriginalMenuBindings.confirm_button, kConfirmAction) |
      pad_tap(kOriginalMenuBindings.back_button, kCancelAction) |
      pad_tap(input::PadButton::menu, kStartAction) |
      pad_tap(input::PadButton::up, kUpAction) |
      pad_tap(input::PadButton::down, kDownAction) |
      pad_tap(input::PadButton::left, kLeftAction) |
      pad_tap(input::PadButton::right, kRightAction);
  result.repeated_actions =
      ui_repeat_.advance({held, result.pressed_actions}, presentation_seconds);
  previous_ui_held_ = held;
  // Original UI mode 2 uses fixed mouse/keyboard aliases, independent of the
  // gameplay binding table. Preserve the fixed navigation repeat stream above.
  constexpr std::uint32_t kConfirm = 0x1000, kBack = 0x2000, kDefaults = 0x8000;
  const auto ui_aliases =
      ((key_state(physical, kOriginalMenuBindings.confirm_keys[0], true) ||
        key_state(physical, kOriginalMenuBindings.confirm_keys[1], true))
           ? kConfirm
           : 0U) |
      ((key_state(physical, kOriginalMenuBindings.back_keys[0], true) ||
        key_state(physical, kOriginalMenuBindings.back_keys[1], true))
           ? kBack
           : 0U);
  const auto defaults =
      (physical.pad_pressed & input::pad_bit(input::PadButton::north)) != 0
          ? kDefaults
          : 0U;
  // The fixed UI receiver expands a wheel impulse into the same three
  // directional namespaces as its arrow input. Gameplay cycling is translated
  // separately below from the saved action bindings.
  constexpr std::uint32_t kWheelUpNavigation = 0x01010001,
                          kWheelDownNavigation = 0x02020002;
  const auto wheel_navigation = physical.wheel > 0   ? kWheelUpNavigation
                                : physical.wheel < 0 ? kWheelDownNavigation
                                                     : 0U;
  result.pressed_actions |= ui_aliases | defaults | wheel_navigation;
  result.repeated_actions |= ui_aliases | defaults | wheel_navigation;
  // The fixed UI receiver retains positional stick Y and adds a two-unit
  // wheel impulse. This is an analog observation, not remapped player motion;
  // wheel and stick may sum outside [-1, 1]. Help owns its pixel conversion.
  constexpr float kWheelAxisImpulse = 2;
  result.menu_vertical_axis =
      physical.movement[1] + (physical.wheel > 0   ? kWheelAxisImpulse
                              : physical.wheel < 0 ? -kWheelAxisImpulse
                                                   : 0.F);
  const auto& buttons = settings.controller_identity == kSourceXinputIdentity
                            ? settings.buttons
                            : kSourceXinputButtons;
  const auto pad = [&](unsigned slot, bool pressed) {
    const auto code = buttons.at(slot);
    return code < kControllerButtonCount &&
           ((pressed ? physical.pad_pressed : physical.pad_held) &
            (1U << code)) != 0;
  };
  const auto key = [&](unsigned row, bool pressed) {
    return key_state(physical, settings.keys.at(row), pressed);
  };
  constexpr unsigned kForward = 0, kBackward = 1, kLeft = 2, kRight = 3,
                     kFire = 4, kAttack = 5, kReload = 6, kDodge = 7,
                     kCrouch = 8, kZoom = 9, kMagic = 10, kCycle = 11,
                     kReverse = 12, kSpellX = 13, kSpellA = 14, kSpellB = 15,
                     kPause = 16;
  constexpr unsigned kPadMenu = 4, kPadCrouch = 6, kPadZoom = 7, kPadLeft = 8,
                     kPadRight = 9, kPadAttack = 10, kPadWeapon = 11,
                     kPadReload = 12, kPadClose = 13, kPadDodge = 14,
                     kPadFire = 15;
  result.movement = {merge_axis(physical.movement[0],
                                static_cast<float>(key(kRight, false)) -
                                    static_cast<float>(key(kLeft, false))),
                     merge_axis(physical.movement[1],
                                static_cast<float>(key(kForward, false)) -
                                    static_cast<float>(key(kBackward, false)))};
  result.look = physical.look;
  // Original mouse sampler keeps one reference-frame delta, attenuating long
  // samples before the consumer's ordinary time-scaled camera update.
  constexpr float kReferenceSeconds = 1.F / 60;
  const auto mouse_scale = sampling_seconds > kReferenceSeconds
                               ? kReferenceSeconds / sampling_seconds
                               : 1.F;
  result.mouse_look = {physical.mouse_delta[0] * mouse_scale,
                       physical.mouse_delta[1] * mouse_scale};
  if (settings.invert_horizontal) {
    result.look[0] = -result.look[0];
    result.mouse_look[0] = -result.mouse_look[0];
  }
  if (settings.invert_vertical) {
    result.look[1] = -result.look[1];
    result.mouse_look[1] = -result.mouse_look[1];
  }
  result.turn_speed = device.turn_speed;
  result.mouse_bias = device.mouse_bias;
  result.fire_trigger = key(kFire, false) || pad(kPadFire, false) ? 1.F : 0.F;
  // Preserve analog trigger magnitude when the mapped fire input is a trigger.
  constexpr unsigned kLeftTrigger = 10, kRightTrigger = 11;
  if (!key(kFire, false) && buttons[kPadFire] == kRightTrigger) {
    result.fire_trigger = physical.right_trigger;
  }
  if (!key(kFire, false) && buttons[kPadFire] == kLeftTrigger) {
    result.fire_trigger = physical.left_trigger;
  }
  result.evade_held =
      key(kDodge, false) || key(kDodge, true) || pad(kPadDodge, false);
  result.crouch_pressed = key(kCrouch, true) || pad(kPadCrouch, true);
  result.aim_mode_pressed = key(kZoom, true) || pad(kPadZoom, true);
  result.pause_pressed = key(kPause, true) || pad(kPadMenu, true);
  result.attack_pressed = key(kAttack, true) || pad(kPadAttack, true);
  result.reload_pressed = key(kReload, true) || pad(kPadReload, true);
  result.weapon_cycle_pressed = key(kCycle, true) || pad(kPadWeapon, true);
  result.keyboard_cycle_forward_pressed = key(kCycle, true);
  result.weapon_cycle_backward_pressed = key(kReverse, true);
  result.keyboard_magic_pressed = key(kMagic, true);
  result.shoulder_left_pressed = pad(kPadLeft, true);
  result.shoulder_right_pressed = pad(kPadRight, true);
  result.shoulder_left_held = pad(kPadLeft, false);
  result.shoulder_right_held = pad(kPadRight, false);
  result.face_north_pressed = pad(kPadClose, true);
  result.face_east_pressed = pad(kPadWeapon, true) || key(kSpellB, true);
  result.face_south_pressed = pad(kPadAttack, true) || key(kSpellA, true);
  result.face_west_pressed = pad(kPadReload, true) || key(kSpellX, true);
  return result;
}
}  // namespace bw::game
