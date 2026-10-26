// Game input translation from neutral physical samples and copied authoritative
// settings. Original scan-code and XInput compatibility stay here; platform
// adapters never read Profile. Fixed mapping arrays produce original gameplay
// actions, preserving the separate fixed UI navigation stream and analog axis.
// Fixed UI scrolling combines positional stick Y with wheel impulses and never
// consumes remapped gameplay movement. Caller supplies one
// sample per presentation update. No owner mutation, allocation or clock read.
// Core I.1/C.9 and performance MEM.9, both phases, make the mapping boundary
// explicit. Source algorithms: native-pause/controller-options.md.
#pragma once
#include <optional>

#include "bw/platform/startup_host.hpp"
#include "controller_settings.hpp"
#include "device_options.hpp"
namespace bw::game {
// Physical capture is independent from remapped actions. It admits the first
// source-ordered key/button press, so rebinding never depends on old bindings.
std::optional<std::uint16_t> captured_keyboard_binding(
    const input::PhysicalState& input);
std::optional<std::uint8_t> captured_controller_binding(
    const input::PhysicalState& input);
std::uint16_t source_key_to_usb(std::uint16_t code);
platform::StartupInput map_controller_input(platform::StartupInput sample,
                                            const ControllerSettings& settings,
                                            DeviceOptionsSnapshot device,
                                            float sampling_seconds);
}  // namespace bw::game
