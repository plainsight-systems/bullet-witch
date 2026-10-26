// Game input translation from neutral physical samples and copied authoritative
// settings. Original scan-code and XInput compatibility stay here; platform
// adapters never read Profile. Fixed mapping arrays produce original gameplay
// actions, preserving the separate fixed UI navigation stream and analog axis.
// Fixed UI scrolling combines positional stick Y with wheel impulses and never
// consumes remapped gameplay movement. Caller supplies one
// sample per pump with separate wall/presentation intervals. No allocation or
// clock read. Fixed UI held/tap -> press/repeat -> aliases -> saved actions.
// Core I.1/C.9 and performance MEM.9, both phases, make the mapping boundary
// explicit (I.1 #ri-explicit, C.9 #rc-private); MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds
// history/storage. Source and pinned reference: neutral-input-boundary.md.
#pragma once
#include <optional>

#include "bw/input/action_repeat.hpp"
#include "bw/input/input_frame.hpp"
#include "game/settings/controller_settings.hpp"
#include "game/settings/device_options.hpp"
#include "mapped_input.hpp"
namespace bw::game {
// Physical capture is independent from remapped actions. It admits the first
// source-ordered key/button press, so rebinding never depends on old bindings.
std::optional<std::uint16_t> captured_keyboard_binding(
    const input::PhysicalState& input);
std::optional<std::uint8_t> captured_controller_binding(
    const input::PhysicalState& input);
std::uint16_t source_key_to_usb(std::uint16_t code);
// One stable application owner advances fixed UI history on every native pump,
// including loading and paused presentation. Invalid time rejects before state
// mutation; mapping does not mutate settings. Game state admits the intentions.
class ControllerInput final {
 public:
  ControllerInput() = default;
  ~ControllerInput() = default;
  ControllerInput(const ControllerInput&) = delete;
  ControllerInput& operator=(const ControllerInput&) = delete;
  ControllerInput(ControllerInput&&) = delete;
  ControllerInput& operator=(ControllerInput&&) = delete;
  MappedInput map(const input::InputFrame& sample,
                  const ControllerSettings& settings,
                  DeviceOptionsSnapshot device, float sampling_seconds,
                  float presentation_seconds);

 private:
  std::uint32_t previous_ui_held_{};
  input::ActionRepeat ui_repeat_;
  // One active prompt family; idle/held devices cannot override new activity.
  std::uint32_t previous_pad_ui_held_{};
  MenuInputDevice prompt_device_{MenuInputDevice::keyboard_mouse};
};
}  // namespace bw::game
