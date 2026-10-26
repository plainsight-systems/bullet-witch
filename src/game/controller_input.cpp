// Source keyboard scan positions are translated from USB observations, then
// duplicate-free saved bindings select held/pressed game intentions. Controller
// compatibility applies the original device-map fallback. Mouse look remains
// a separate delta, so camera settings do not clamp it into a stick range.
// Original mapping arrays are bounded; no per-frame allocation. Both-phase
// Core I.1/E.4 and performance MEM.9 apply as in controller_input.hpp.
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
platform::StartupInput map_controller_input(platform::StartupInput result,
                                            const ControllerSettings& settings,
                                            DeviceOptionsSnapshot device,
                                            float sampling_seconds) {
  if (!std::isfinite(sampling_seconds) || sampling_seconds < 0) {
    throw std::invalid_argument("Invalid input sampling interval");
  }
  const auto& physical = result.physical;
  // Original UI mode 2 uses fixed mouse/keyboard aliases, independent of the
  // gameplay binding table. Preserve the platform's navigation repeat stream.
  constexpr std::uint32_t kConfirm = 0x1000, kBack = 0x2000, kDefaults = 0x8000;
  const auto ui_aliases =
      ((key_state(physical, 0x12c, true) || key_state(physical, 0x39, true))
           ? kConfirm
           : 0U) |
      ((key_state(physical, 0x12d, true) || key_state(physical, 0x2a, true))
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
