// Resolve native mode-zero kico descriptors against copied input bindings.
// Source action IDs map through 472930/472dd0; literal ico never enters here.
// Both-phase Core I.1/C.9 and MEM.9 apply as documented in the header.
#include "help_input_icons.hpp"

#include <algorithm>
#include <cmath>
#include <span>

#include "input_icon.hpp"
namespace bw::game {
namespace {
// Native scales the icon rectangle about an advancing pivot. This is its
// resulting canvas pitch; text advance is the independent native measure.
constexpr float kDrawScale = .933333993F, kPivotStride = 64,
                kPitch = kPivotStride * kDrawScale * (1 - kDrawScale / 3),
                kAdvance = 128.F / 3, kInset = 32.F / 15;
constexpr unsigned kNoBinding = 0x100, kMouseCameraIcon = 99, kMoveIcon = 100,
                   kCameraIcon = 101;
}  // namespace
HelpInputIcons help_input_icons(const ControllerSettings& settings,
                                bool controller_connected) {
  HelpInputIcons result;
  const auto& buttons = settings.controller_identity == kSourceXinputIdentity
                            ? settings.buttons
                            : kSourceXinputButtons;
  constexpr std::array<unsigned, 6> kPadSlots{0, 0, 15, 14, 10, 12};
  constexpr std::array<unsigned, 6> kKeySlots{0, 0, 4, 7, 5, 6};
  for (unsigned action = 0; action < result.actions.size(); ++action) {
    auto& run = result.actions.at(action);
    run.pitch = kPitch;
    const auto append = [&](unsigned icon) {
      if (std::ranges::find(std::span(run.icons).first(run.count), icon) ==
          std::span(run.icons).first(run.count).end()) {
        run.icons.at(run.count++) = static_cast<std::uint16_t>(icon);
      }
    };
    if (controller_connected) {
      if (action == static_cast<unsigned>(HelpInputAction::movement)) {
        append(kMoveIcon);
      } else if (action == static_cast<unsigned>(HelpInputAction::camera)) {
        append(kCameraIcon);
      } else {
        append(input_pad_icon(buttons.at(kPadSlots.at(action))));
      }
    } else if (action == static_cast<unsigned>(HelpInputAction::movement)) {
      constexpr unsigned kMovementKeys = 4;
      for (unsigned key = 0; key < kMovementKeys; ++key) {
        if (settings.keys.at(key) != kNoBinding) {
          append(input_key_icon(settings.keys.at(key)));
        }
      }
    } else if (action == static_cast<unsigned>(HelpInputAction::camera)) {
      append(kMouseCameraIcon);
    } else if (settings.keys.at(kKeySlots.at(action)) != kNoBinding) {
      append(input_key_icon(settings.keys.at(kKeySlots.at(action))));
    }
    run.advance = static_cast<float>(run.count) * kAdvance + kInset;
  }
  return result;
}
bool valid_help_input_icons(const HelpInputIcons& icons) noexcept {
  return std::ranges::all_of(icons.actions, [](const auto& run) {
    return run.count <= run.icons.size() && std::isfinite(run.pitch) &&
           run.pitch >= 0 && std::isfinite(run.advance) && run.advance >= 0 &&
           std::ranges::all_of(std::span(run.icons).first(run.count),
                               [](auto id) { return id < kInputIconCount; });
  });
}
}  // namespace bw::game
