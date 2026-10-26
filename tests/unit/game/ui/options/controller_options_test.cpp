// Public in-memory input/settings contracts: preserve unknown original bytes,
// reject invalid storage without publication, retry failed persistence, capture
// through physical release transitions and route copied mappings live. Core E.4
// and performance MEM.9 (both phases, controller_settings.hpp) require bounded
// fixtures and invariant checks. This is not a native-device or rendered test.
#include "game/ui/options/controller_options.hpp"

#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "game/player/input/controller_input.hpp"
#include "game/ui/common/sprite_color.hpp"
#include "game/ui/options/options.hpp"
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
class Storage final : public bw::platform::SaveStorage {
 public:
  bw::platform::SaveRead read(std::string_view name, std::size_t) override {
    const auto found = files.find(std::string(name));
    return found == files.end()
               ? bw::platform::SaveRead{bw::platform::SaveStatus::missing, {}}
               : bw::platform::SaveRead{bw::platform::SaveStatus::ok,
                                        found->second};
  }
  bw::platform::SaveStatus write(std::string_view name,
                                 std::span<const std::byte> bytes) override {
    if (fail) return bw::platform::SaveStatus::io_error;
    files[std::string(name)] = {bytes.begin(), bytes.end()};
    return bw::platform::SaveStatus::ok;
  }
  bool fail{};
  std::map<std::string, std::vector<std::byte>> files;
};
void settings_contract() {
  std::array<std::byte, 72> bytes{};
  bytes.fill(std::byte{0x90});
  auto value = bw::game::controller_default_settings();
  value.invert_vertical = true;
  bw::game::encode_controller_settings(bytes, value);
  require(bytes[0] == std::byte{0x90} && bytes[70] == std::byte{0x90} &&
              bytes[12] == std::byte{0x91} && bytes[13] == std::byte{0x90},
          "controller updates preserve unowned bytes and flag bits");
  require(bw::game::decode_controller_settings(bytes) == value,
          "controller original section round trip");
  bw::game::remap_controller_key(value, 0, 0x1f);
  require(value.keys[0] == 0x1f && value.keys[1] == 0x11,
          "duplicate key swaps old binding");
  bw::game::remap_controller_button(value, 0, 10);
  require(value.controller_identity == bw::game::kSourceXinputIdentity &&
              value.buttons[15] == 10 && value.buttons[14] == 11,
          "first pad edit initializes source map and swaps trigger");
}
void device_contract() {
  Storage storage;
  bw::game::DeviceOptions owner(storage);
  require(owner.load() == bw::platform::SaveStatus::ok,
          "missing device settings use original defaults");
  auto settings = owner.snapshot();
  settings.mouse_bias = 100;
  settings.turn_speed = 1;
  settings.vibration = false;
  owner.control().apply(settings);
  auto invalid = settings;
  invalid.turn_speed = 0;
  bool rejected = false;
  try {
    owner.control().apply(invalid);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && owner.snapshot() == settings,
          "invalid device edit preserves previous settings");
  storage.fail = true;
  require(owner.save_control().save() == bw::platform::SaveStatus::io_error,
          "device persistence exposes failure");
  storage.fail = false;
  require(owner.save_control().save() == bw::platform::SaveStatus::ok &&
              storage.files.size() == 1,
          "failed device persistence retries");
  bw::game::DeviceOptions loaded(storage);
  require(loaded.load() == bw::platform::SaveStatus::ok &&
              loaded.snapshot() == settings,
          "device options reload");
  storage.files.begin()->second.back() = std::byte{0};
  require(loaded.load() == bw::platform::SaveStatus::invalid &&
              loaded.snapshot() == settings,
          "malformed storage cannot publish");
}
void input_contract() {
  bw::input::InputFrame physical;
  bw::game::ControllerInput mapper;
  physical.physical.keys_held.set(26);    // USB W position
  physical.physical.keys_pressed.set(8);  // USB E position
  physical.physical.mouse_held = 1;
  physical.physical.look = {.25F, -.5F};
  physical.physical.mouse_delta = {20, -10};
  auto settings = bw::game::controller_default_settings();
  settings.invert_horizontal = true;
  settings.invert_vertical = true;
  const auto mapped = mapper.map(physical, settings, {}, 1.F / 30, 1.F / 30);
  require(mapped.movement[1] == 1 && mapped.fire_trigger == 1 &&
              mapped.aim_mode_pressed && mapped.look[0] == -.25F &&
              mapped.look[1] == .5F && mapped.mouse_look[0] == -10 &&
              mapped.mouse_look[1] == 5 && mapped.menu_vertical_axis == 0,
          "live original keyboard, inversion and mouse interval mapping");
  physical = {};
  physical.physical.wheel = 1;
  const auto wheel = mapper.map(physical, settings, {}, 0, 0);
  require(
      wheel.weapon_cycle_backward_pressed && !wheel.weapon_cycle_pressed &&
          wheel.pressed_actions == 0x01010001U &&
          wheel.repeated_actions == 0x01010001U &&
          wheel.menu_vertical_axis == 2,
      "wheel up preserves backward weapon cycling and original UI navigation");
  physical.physical.wheel = -1;
  const auto wheel_down = mapper.map(physical, settings, {}, 0, 0);
  require(
      wheel_down.weapon_cycle_pressed &&
          !wheel_down.weapon_cycle_backward_pressed &&
          wheel_down.pressed_actions == 0x02020002U &&
          wheel_down.repeated_actions == 0x02020002U &&
          wheel_down.menu_vertical_axis == -2,
      "wheel down preserves forward weapon cycling and original UI navigation");
  physical.physical.movement[1] = -.25F;
  require(
      mapper.map(physical, settings, {}, 0, 0).menu_vertical_axis == -2.25F,
      "fixed UI retains combined stick and wheel magnitude beyond unit range");
  physical.physical.movement[1] = 0;
  physical.physical.wheel = 0;
  const auto released_wheel = mapper.map(physical, settings, {}, 0, 0);
  require(released_wheel.pressed_actions == 0 &&
              released_wheel.repeated_actions == 0 &&
              !released_wheel.weapon_cycle_pressed &&
              !released_wheel.weapon_cycle_backward_pressed &&
              released_wheel.menu_vertical_axis == 0,
          "wheel impulse does not leave a navigation hold or cycle behind");
  physical = {};
  physical.physical.pad_pressed =
      bw::input::pad_bit(bw::input::PadButton::north);
  require(
      (mapper.map(physical, settings, {}, 0, 0).pressed_actions & 0x8000U) != 0,
      "fixed UI north button restores default settings");
  physical = {};
  physical.physical.pad_held = bw::input::pad_bit(bw::input::PadButton::menu);
  require(mapper.map(physical, settings, {}, 0, 0).pressed_actions == 0x10,
          "Start remains distinct from ordinary menu confirmation");
  physical = {};
  physical.physical.mouse_pressed = 3;
  const auto fixed_ui = mapper.map(physical, settings, {}, 0, 0);
  require(fixed_ui.pressed_actions == 0x3000U,
          "fixed UI mouse aliases remain independent of gameplay remapping");
  physical.physical.mouse_pressed = 0;
  physical.physical.keys_pressed.set(225);
  require(
      (mapper.map(physical, settings, {}, 0, 0).pressed_actions & 0x2020U) != 0,
      "original left Shift supplies fixed UI back");
  physical = {};
  physical.physical.mouse_pressed = 4;
  require(mapper.map(physical, settings, {}, 0, 0).keyboard_magic_pressed,
          "source middle mouse opens magic ring");
}
// Fixed UI history has one owner independent of remapped gameplay and clock.
void input_history_contract() {
  static_assert(!std::is_copy_constructible_v<bw::game::ControllerInput>);
  static_assert(!std::is_move_constructible_v<bw::game::ControllerInput>);
  bw::game::ControllerInput mapper;
  auto settings = bw::game::controller_default_settings();
  bw::input::InputFrame frame;
  frame.physical.keys_pressed.set(40);
  const auto tap = mapper.map(frame, settings, {}, 0, 0);
  require(tap.pressed_actions == 0x1000 && tap.repeated_actions == 0x1000,
          "key down/up drained in one pump retains one confirmation");
  frame = {};
  require(mapper.map(frame, settings, {}, 0, 0).pressed_actions == 0,
          "short tap never becomes retained held input");
  frame.physical.keys_held.set(82);
  auto mapped = mapper.map(frame, settings, {}, 8, .25F);
  require(mapped.pressed_actions == 0x01000000 &&
              mapped.repeated_actions == 0x01000000,
          "first direction press is independent of wall sample duration");
  mapped = mapper.map(frame, settings, {}, 0, .125F);
  require(mapped.pressed_actions == 0 && mapped.repeated_actions == 0,
          "held direction does not repeat before original initial delay");
  mapped = mapper.map(frame, settings, {}, 0, .125F);
  require(mapped.repeated_actions == 0x01000000,
          "direction repeat uses supplied presentation time through pause");
  mapped = mapper.map(frame, settings, {}, 0, .125F);
  require(mapped.repeated_actions == 0x01000000,
          "repeat retains original subtraction cadence");
  frame = {};
  (void)mapper.map(frame, settings, {}, 0, 0);
  frame.physical.keys_held.set(82);
  mapped = mapper.map(frame, settings, {}, 0, 0);
  require(mapped.pressed_actions == 0x01000000,
          "focus-cleared state rearms next held observation");
  bool rejected = false;
  try {
    (void)mapper.map({}, settings, {}, 0,
                     std::numeric_limits<float>::quiet_NaN());
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(
      rejected && mapper.map(frame, settings, {}, 0, 0).pressed_actions == 0,
      "invalid time rejects without retiring valid held history");
  frame = {};
  (void)mapper.map(frame, settings, {}, 0, 0);
  frame.physical.movement = {.9F, -.8F};
  frame.physical.movement_minimum = {-.7F, -.8F};
  frame.physical.movement_maximum = {.9F, .6F};
  mapped = mapper.map(frame, settings, {}, 0, 0);
  require(mapped.pressed_actions == 0x0f000000 &&
              mapped.movement == frame.physical.movement,
          "opposing controller axes preserve every fixed direction and merged "
          "movement");
  frame = {};
  (void)mapper.map(frame, settings, {}, 0, 0);
  bw::game::remap_controller_button(
      settings, 4, static_cast<std::uint8_t>(bw::input::PadButton::north));
  frame.physical.pad_held = bw::input::pad_bit(bw::input::PadButton::south);
  frame.physical.pad_pressed = frame.physical.pad_held;
  mapped = mapper.map(frame, settings, {}, 0, 0);
  require(mapped.pressed_actions == 0x1000 && !mapped.attack_pressed,
          "fixed face confirmation does not follow remapped gameplay attack");
}
void capture_contract() {
  Storage storage;
  bw::game::Profile profile;
  (void)profile.load(storage);
  bw::game::DeviceOptions device(storage);
  (void)device.load();
  bw::game::ControllerOptionsFlow flow(profile.settings_control(),
                                       device.control());
  constexpr bw::game::MenuInput confirm{0x1010, 0}, back{0x2020, 0},
      left{0, 0x04000000};
  flow.start();
  flow.update({}, {}, 2);
  flow.update(confirm, {}, 0);  // enter keyboard
  bw::input::PhysicalState physical;
  physical.keys_held.set(40);  // originating Enter must release
  flow.update(confirm, physical, 0);
  physical.keys_pressed.set(22);
  flow.update({}, physical, 0);
  require(profile.settings_control().controller_settings().keys[0] == 0x11,
          "capture ignores activation key until release");
  flow.update({}, {}, 0);
  physical = {};
  physical.keys_pressed.set(22);
  physical.keys_held.set(22);
  flow.update({}, physical, 0);
  require(profile.settings_control().controller_settings().keys[0] == 0x1f,
          "capture publishes source key and duplicate swap");
  flow.update({}, {}, 0);
  flow.update(back, {}, 0);
  flow.update(back, {}, 0);
  require(flow.presentation().dialog == bw::game::ControllerDialog::discard &&
              !flow.presentation().dialog_yes,
          "changed Back asks discard, default No");
  flow.update({}, {}, .4F);
  flow.update(left, {}, 0);
  flow.update(confirm, {}, 0);
  flow.update({}, {}, .4F);
  flow.update({}, {}, 2);
  require(flow.completed() &&
              profile.settings_control().controller_settings().keys[0] == 0x11,
          "discard restores entry settings and exits");
}
void child_fade_contract() {
  Storage storage;
  bw::game::Profile profile;
  (void)profile.load(storage);
  bw::game::DeviceOptions device(storage);
  (void)device.load();
  bw::game::OptionsFlow flow({profile.settings_control(),
                              profile.settings_save_control(storage),
                              device.control(), device.save_control()});
  flow.start(0);
  flow.update({}, 2);
  constexpr bw::game::MenuInput down{0, 0x02000000}, confirm{0x1010, 0},
      back{0x2020, 0};
  for (unsigned i = 0; i < 3; ++i) flow.update(down, 0);
  flow.update(confirm, 0);
  flow.update({}, .125F);
  require(flow.presentation().page == bw::game::OptionsPage::options &&
              flow.presentation().opacity == .5F,
          "Options retains ownership while fading to controller child");
  flow.update({}, .125F);
  require(flow.presentation().page == bw::game::OptionsPage::controls &&
              flow.presentation().opacity == 0,
          "controller starts only at black boundary");
  flow.update({}, .125F);
  require(flow.presentation().opacity == .5F,
          "controller source quarter-second reveal");
  flow.update({}, 2);
  flow.update(back, 0);
  flow.update({}, .45F);
  require(flow.presentation().opacity == 1,
          "controller waits source .45 before exit fade");
  flow.update({}, .125F);
  require(
      flow.presentation().opacity > .49F && flow.presentation().opacity < .51F,
      "controller fade controls copied composition opacity");
  flow.update({}, .13F);
  require(flow.presentation().page == bw::game::OptionsPage::options &&
              flow.presentation().opacity == 0,
          "controller returns to Options at black");
  flow.update({}, .25F);
  require(flow.presentation().opacity == 1,
          "Options source quarter-second return reveal");
}
}  // namespace
void controller_options_contracts() {
  const auto tint =
      bw::game::original_sprite_tint({.8F, .25F, 0, .5F}, {.4F, .4F, -.2F, 0});
  require(tint.red == 1 && tint.green == .65F && tint.blue == 0 &&
              tint.alpha == .5F,
          "source COLOR saturation occurs before texture modulation, "
          "preserving alpha");
  settings_contract();
  device_contract();
  input_contract();
  input_history_contract();
  capture_contract();
  child_fade_contract();
}
