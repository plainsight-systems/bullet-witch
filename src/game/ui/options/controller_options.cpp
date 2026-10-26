// Retain explicit navigation/capture phases and call only the settings
// capabilities required by each source transition. Rejected bounds produce the
// original error cue without mutation. Snapshot refresh follows successful
// updates; outer Options owns storage. Core E.4 and performance MEM.9 were
// reread for fixed transactional implementation; see header for full contracts.
#include "controller_options.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "game/player/input/controller_input.hpp"
namespace bw::game {
namespace {
constexpr float kEntrySeconds = 1.8F, kExitWait = .45F, kFadeSeconds = .25F,
                kExitSeconds = kExitWait + kFadeSeconds,
                kDescriptionResponse = .17F, kPanelSoundDelay = .4F,
                kChangedDescription = .5F;
constexpr unsigned kSectionCount = 5, kMinimum = 1, kMaximum = 100;
constexpr std::array<unsigned, 3> kRowCounts{17, 11, 5};
unsigned wrapped(unsigned value, int step, unsigned count) {
  return step < 0 ? (value == 0 ? count - 1 : value - 1) : (value + 1) % count;
}
bool released(const input::PhysicalState& value) {
  return value.keys_held.none() && value.pad_held == 0 &&
         value.mouse_held == 0 && value.wheel == 0;
}
}  // namespace
void ControllerOptionsFlow::refresh() {
  view_.settings = settings_.controller_settings();
  view_.device = device_.snapshot();
}
void ControllerOptionsFlow::start() {
  if (view_.active) {
    throw std::logic_error("Controller configuration already active");
  }
  view_ = {};
  view_.active = view_.opening = true;
  view_.opacity = 0;
  capture_ = Capture::none;
  description_ = {};
  pulse_phase_ = 0;
  sounds_ = {};
  refresh();
  baseline_ = view_.settings;
  device_baseline_ = view_.device;
  sounds_.add(MenuSound::panel, kPanelSoundDelay);
}
void ControllerOptionsFlow::close() {
  view_.exiting = true;
  view_.elapsed = 0;
  view_.editing = false;
  view_.opacity = 1;
}
void ControllerOptionsFlow::change_misc(int direction) {
  auto settings = settings_.controller_settings();
  auto device = device_.snapshot();
  switch (view_.row) {
    case 0:
      settings.invert_vertical = !settings.invert_vertical;
      break;
    case 1:
      settings.invert_horizontal = !settings.invert_horizontal;
      break;
    case 2:
      device.vibration = !device.vibration;
      break;
    default: {
      auto& value = view_.row == 3 ? device.mouse_bias : device.turn_speed;
      if ((direction < 0 && value == kMinimum) ||
          (direction > 0 && value == kMaximum)) {
        sounds_.add(MenuSound::error);
        return;
      }
      value = direction < 0 ? value - 1 : value + 1;
    }
  }
  settings_.set_controller_settings(settings);
  device_.apply(device);
  sounds_.add(MenuSound::cursor);
}
void ControllerOptionsFlow::update_policy(
    MenuInput menu, const input::PhysicalState& physical) {
  constexpr std::uint32_t kConfirm = 0x1010, kCancel = 0x2020, kUp = 0x01000000,
                          kDown = 0x02000000, kLeft = 0x04000000,
                          kRight = 0x08000000;
  const bool confirm = (menu.pressed & kConfirm) != 0,
             back = (menu.pressed & kCancel) != 0,
             up = (menu.repeated & kUp) != 0,
             down = (menu.repeated & kDown) != 0,
             left = (menu.repeated & kLeft) != 0,
             right = (menu.repeated & kRight) != 0;
  if (capture_ != Capture::none) {
    if (capture_ == Capture::await_release && released(physical)) {
      capture_ = Capture::wait;
    } else if (capture_ == Capture::after_release && released(physical)) {
      capture_ = Capture::none;
    } else if (capture_ == Capture::wait) {
      auto candidate = settings_.controller_settings();
      bool captured = false;
      if (view_.selected == ControllerSection::keyboard) {
        if (const auto key = captured_keyboard_binding(physical)) {
          remap_controller_key(candidate, view_.row, *key);
          captured = true;
        }
      } else if (const auto button = captured_controller_binding(physical)) {
        remap_controller_button(candidate, view_.row, *button);
        captured = true;
      }
      if (captured) {
        settings_.set_controller_settings(candidate);
        capture_ = Capture::after_release;
        sounds_.add(MenuSound::confirm);
      }
      if (view_.selected == ControllerSection::gamepad &&
          !physical.controller_connected) {
        capture_ = Capture::none;
      }
    }
    return;
  }
  if (up || down) {
    if (view_.editing) {
      view_.row = wrapped(view_.row, up ? -1 : 1,
                          kRowCounts.at(static_cast<unsigned>(view_.selected)));
    } else {
      view_.selected = static_cast<ControllerSection>(wrapped(
          static_cast<unsigned>(view_.selected), up ? -1 : 1, kSectionCount));
      if (view_.selected <= ControllerSection::miscellaneous) {
        view_.displayed = view_.selected;
      }
    }
    description_.value = kChangedDescription;
    sounds_.add(MenuSound::cursor);
    return;
  }
  if (back) {
    sounds_.add(MenuSound::back);
    if (view_.editing) {
      view_.editing = false;
    } else if (settings_.controller_settings() != baseline_ ||
               device_.snapshot() != device_baseline_) {
      view_.dialog = ControllerDialog::discard;
      view_.dialog_yes = false;
      dialog_.start(false);
      view_.dialog_view = dialog_.presentation();
      const auto requests = dialog_.take_sounds();
      for (const auto& cue :
           std::span(requests.requests).first(requests.size)) {
        sounds_.add(cue.sound, cue.delay_seconds);
      }
    } else {
      close();
    }
    return;
  }
  if (view_.editing) {
    if (view_.selected == ControllerSection::miscellaneous) {
      if (left || right) change_misc(left ? -1 : 1);
    } else if (confirm) {
      if (view_.selected == ControllerSection::gamepad &&
          !physical.controller_connected) {
        sounds_.add(MenuSound::error);
      } else {
        capture_ = Capture::await_release;
        sounds_.add(MenuSound::confirm);
      }
    }
  } else if (confirm) {
    sounds_.add(MenuSound::confirm);
    if (view_.selected == ControllerSection::exit) {
      close();
    } else if (view_.selected == ControllerSection::defaults) {
      view_.dialog = ControllerDialog::defaults;
      view_.dialog_yes = false;
      dialog_.start(false);
      view_.dialog_view = dialog_.presentation();
      const auto requests = dialog_.take_sounds();
      for (const auto& cue :
           std::span(requests.requests).first(requests.size)) {
        sounds_.add(cue.sound, cue.delay_seconds);
      }
    } else {
      view_.editing = true;
      view_.row = 0;
      description_.value = kChangedDescription;
    }
  }
}
void ControllerOptionsFlow::update(MenuInput menu,
                                   const input::PhysicalState& physical,
                                   float seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid controller menu interval");
  }
  sounds_ = {};
  if (!view_.active) return;
  view_.elapsed += seconds;
  if (view_.opening) {
    view_.opacity = std::clamp(view_.elapsed / kFadeSeconds, 0.F, 1.F);
    if (view_.elapsed >= kEntrySeconds) {
      view_.opening = false;
      view_.elapsed = 0;
    }
  } else if (view_.exiting) {
    view_.opacity =
        1 - std::clamp((view_.elapsed - kExitWait) / kFadeSeconds, 0.F, 1.F);
    if (view_.elapsed >= kExitSeconds) view_.active = false;
  } else if (view_.dialog != ControllerDialog::none) {
    dialog_.update(menu, seconds);
    sounds_ = dialog_.take_sounds();
    view_.dialog_view = dialog_.presentation();
    view_.dialog_yes = view_.dialog_view.yes;
    if (view_.dialog_view.complete) {
      if (view_.dialog_view.yes) {
        if (view_.dialog == ControllerDialog::defaults) {
          settings_.set_controller_settings(controller_default_settings());
          device_.apply({});
          view_.row = 0;
        } else {
          settings_.set_controller_settings(baseline_);
          device_.apply(device_baseline_);
          close();
        }
      }
      view_.dialog = ControllerDialog::none;
    }
  } else {
    update_policy(menu, physical);
  }
  refresh();
  view_.capturing = capture_ != Capture::none;
  description_ = math::damped_response(
      description_, math::DampedResponseTarget{1.F, kDescriptionResponse},
      seconds);
  view_.description_alpha = std::clamp(description_.value, 0.F, 1.F);
  pulse_phase_ = std::fmod(pulse_phase_ + seconds, 1.F);
  const auto pulse = math::periodic_sine_interpolate(
      math::PeriodicSineEndpoints{0.F, .5F}, pulse_phase_);
  view_.marker_brightness = 1 + pulse * pulse;
}
}  // namespace bw::game
