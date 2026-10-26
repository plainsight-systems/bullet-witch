// Source Options applies live System edits and restores its entry snapshot on
// confirmed discard. Save errors retain edits and expose the original retry
// choice. Presentation owns no backend gain or physical input policy. Costume
// changes publish a checked request and retain prior data on transaction
// failure. Core E.4 (#re-design-invariants) and performance MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md), reread for
// implementation, keep invalid inputs outside state mutation and ordinary
// updates allocation-free.
#include "pause_options.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bw::game {
namespace {
constexpr std::uint32_t kConfirm = 0x1000, kStart = 0x10, kCancel = 0x2020,
                        kUp = 0x01000000, kDown = 0x02000000,
                        kHorizontal = 0x0c000000;
constexpr unsigned kCostumes = 6, kOptions = 8, kFullVolume = 256;
constexpr float kOptionsSeconds = 1.8F, kBrightnessSeconds = .25F;
constexpr std::uint32_t kLeft = 0x04000000, kReset = 0x8000;
constexpr unsigned kVolumeStep = 16;
}  // namespace
PauseOptionsFlow::PauseOptionsFlow(PauseOptionsServices services)
    : services_(services), controllers_(services.settings, services.device) {
  observe_settings();
}
void PauseOptionsFlow::observe_settings() {
  view_.settings.subtitles = services_.settings.subtitles_enabled();
  view_.settings.autosave = services_.settings.autosave_enabled();
  view_.settings.music = services_.settings.music_volume();
  view_.settings.effects = services_.settings.effects_volume();
}
bool PauseOptionsFlow::changed() const {
  return baseline_ && (services_.settings.changed_since(*baseline_) ||
                       services_.device.snapshot() != device_baseline_);
}
void PauseOptionsFlow::begin_exit() {
  view_.page = PauseOptionsPage::options;
  view_.elapsed = 0;
  view_.exiting = true;
}
void PauseOptionsFlow::dialog(PauseOptionsPage page, bool yes) {
  view_.page = page;
  dialog_.start(yes, page == PauseOptionsPage::saved
                         ? MenuDialogMode::acknowledgment
                         : MenuDialogMode::yes_no);
  const auto batch = dialog_.take_sounds();
  for (std::size_t i = 0; i < batch.size; ++i) {
    sounds_.add(batch.requests.at(i).sound);
  }
  view_.dialog_yes = yes;
  view_.dialog_closing = false;
  view_.dialog_progress = 0;
  view_.elapsed = 0;
}
void PauseOptionsFlow::save() {
  const auto system = services_.save_settings.save();
  const auto device = services_.save_device.save();
  if (system == platform::SaveStatus::ok &&
      device == platform::SaveStatus::ok) {
    dialog(PauseOptionsPage::saved, false);
    sounds_.add(MenuSound::save);
  } else {
    dialog(PauseOptionsPage::save_prompt, true);
  }
}
void PauseOptionsFlow::update_dialog(MenuInput input, float seconds) {
  dialog_.update(input, seconds);
  const auto batch = dialog_.take_sounds();
  for (std::size_t i = 0; i < batch.size; ++i) {
    sounds_.add(batch.requests.at(i).sound);
  }
  const auto state = dialog_.presentation();
  view_.dialog_yes = state.yes;
  view_.dialog_closing = state.closing;
  view_.dialog_progress = state.progress;
  if (!state.complete) {
    return;
  }
  const auto page = view_.page;
  view_.page = PauseOptionsPage::options;
  if (page == PauseOptionsPage::saved ||
      (page == PauseOptionsPage::save_prompt && !state.yes)) {
    begin_exit();
  } else if (page == PauseOptionsPage::discard) {
    if (state.yes) {
      if (!baseline_) {
        throw std::logic_error("Options entry snapshot missing");
      }
      services_.settings.restore(*baseline_);
      services_.device.apply(device_baseline_);
      observe_settings();
      begin_exit();
    }
  } else if (state.yes) {
    save();
  }
}
void PauseOptionsFlow::change_setting(int direction, bool reset) {
  const auto selected = view_.selected;
  bool altered = false;
  if (reset && selected == PauseOption::autosave) {
    return;
  }
  if (selected == PauseOption::subtitles || selected == PauseOption::autosave) {
    const bool old = selected == PauseOption::subtitles
                         ? view_.settings.subtitles
                         : view_.settings.autosave;
    const bool value = reset || !old;
    altered = old != value;
    if (selected == PauseOption::subtitles) {
      services_.settings.set_subtitles(value);
    } else {
      services_.settings.set_autosave(value);
    }
  } else if (selected == PauseOption::music ||
             selected == PauseOption::effects) {
    const auto old = selected == PauseOption::music ? view_.settings.music
                                                    : view_.settings.effects;
    const auto value = reset
                           ? kFullVolume
                           : static_cast<unsigned>(std::clamp(
                                 static_cast<int>(old) +
                                     direction * static_cast<int>(kVolumeStep),
                                 0, static_cast<int>(kFullVolume)));
    altered = old != value;
    if (selected == PauseOption::music) {
      services_.settings.set_music_volume(value);
    } else {
      services_.settings.set_effects_volume(value);
    }
  } else {
    return;
  }
  sounds_.add(altered ? (reset ? MenuSound::confirm : MenuSound::cursor)
                      : MenuSound::error);
  observe_settings();
}
void PauseOptionsFlow::start(unsigned current_costume) {
  if (!completed()) {
    throw std::logic_error("Options already active");
  }
  if (current_costume >= kCostumes) {
    throw std::invalid_argument("Invalid costume");
  }
  baseline_ = services_.settings.checkpoint();
  device_baseline_ = services_.device.snapshot();
  observe_settings();
  sounds_ = {};
  constexpr float kPanelSoundDelay = .2F;
  sounds_.add(MenuSound::panel, kPanelSoundDelay);
  view_.settings.costume = current_costume;
  view_.page = PauseOptionsPage::options;
  view_.selected = PauseOption::subtitles;
  view_.elapsed = 0;
  view_.opening = true;
  view_.opacity = 1;
  child_transition_ = ChildTransition::none;
  child_transition_seconds_ = 0;
  view_.exiting = false;
  view_.notice = {};
  costume_request_.reset();
  description_ = {};
  pulse_phase_ = 0;
}
void PauseOptionsFlow::publish_costumes() {
  costume_ = {};
  costume_.page = MenuPage::costume;
  costume_.title = "Change Costume";
  costume_.message = "A: Select   B: Back";
  costume_.count = kCostumes;
  costume_.selected = view_.settings.costume;
  for (unsigned i = 0; i < kCostumes; ++i) {
    costume_.rows.at(i) = {"Costume", {}, true, i};
  }
}
void PauseOptionsFlow::update(MenuInput input, float seconds,
                              const input::PhysicalState& physical) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid options presentation interval");
  }
  sounds_ = {};
  const auto previous = view_.selected;
  constexpr float kChildFadeSeconds = .25F;
  if (child_transition_ != ChildTransition::none) {
    child_transition_seconds_ += seconds;
    const auto fraction =
        std::clamp(child_transition_seconds_ / kChildFadeSeconds, 0.F, 1.F);
    view_.opacity = child_transition_ == ChildTransition::entering_controls
                        ? 1 - fraction
                        : fraction;
    if (fraction >= 1) {
      if (child_transition_ == ChildTransition::entering_controls) {
        controllers_.start();
        sounds_ = controllers_.take_sounds();
        view_.page = PauseOptionsPage::controls;
        view_.opacity = 0;
      }
      child_transition_ = ChildTransition::none;
      child_transition_seconds_ = 0;
    }
    // Source task animation owners continue advancing through child fades;
    // only navigation admission waits at this composition boundary.
  } else if (view_.page == PauseOptionsPage::controls) {
    controllers_.update(input, physical, seconds);
    const auto child_sounds = controllers_.take_sounds();
    for (std::size_t i = 0; i < child_sounds.size; ++i) {
      const auto cue = child_sounds.requests.at(i);
      sounds_.add(cue.sound, cue.delay_seconds);
    }
    view_.opacity = controllers_.presentation().opacity;
    if (controllers_.completed()) {
      view_.page = PauseOptionsPage::options;
      view_.opacity = 0;
      child_transition_ = ChildTransition::returning_controls;
      child_transition_seconds_ = 0;
    }
    observe_settings();
  } else {
    update_policy(input, seconds);
  }
  constexpr float kChangedDescriptionAlpha = .5F;
  if (previous != view_.selected) {
    description_.value = kChangedDescriptionAlpha;
  }
  constexpr float kDescriptionSeconds = .17F;
  const auto target = view_.opening || view_.exiting || completed() ? 0.F : 1.F;
  description_ = math::damped_response(description_,
                                       {target, kDescriptionSeconds}, seconds);
  view_.description_alpha = description_.value;
  view_.arrow_offset = math::half_sine_interpolate(
      math::PeriodicSineEndpoints<float>{-3, 3}, pulse_phase_);
  const auto marker = math::periodic_sine_interpolate(
      math::PeriodicSineEndpoints<float>{0, .5F}, pulse_phase_);
  view_.marker_brightness = 1 + marker * marker;
  pulse_phase_ = std::fmod(pulse_phase_ + seconds, 1.F);
}
void PauseOptionsFlow::update_policy(MenuInput input, float seconds) {
  if (completed() || costume_request_) {
    return;
  }
  if (view_.page == PauseOptionsPage::discard ||
      view_.page == PauseOptionsPage::save_prompt ||
      view_.page == PauseOptionsPage::saved) {
    update_dialog(input, seconds);
    return;
  }
  if (view_.opening || view_.exiting) {
    view_.elapsed += seconds;
    const auto duration = view_.page == PauseOptionsPage::brightness
                              ? kBrightnessSeconds
                              : kOptionsSeconds;
    if (view_.elapsed > duration) {
      view_.elapsed = duration;
      view_.opening = false;
      if (view_.exiting) {
        view_.page = view_.page == PauseOptionsPage::brightness
                         ? PauseOptionsPage::options
                         : PauseOptionsPage::complete;
        view_.exiting = false;
        view_.elapsed = kOptionsSeconds;
      }
    }
    return;
  }
  const bool confirm = (input.pressed & kConfirm) != 0;
  const bool cancel = (input.pressed & kCancel) != 0;
  const bool start = !confirm && (input.pressed & kStart) != 0;
  if (view_.page == PauseOptionsPage::brightness) {
    if (confirm || start || cancel) {
      sounds_.add(MenuSound::back);
      view_.exiting = true;
      view_.elapsed = 0;
    }
    return;
  }
  if (view_.page == PauseOptionsPage::notice) {
    if (confirm || start || cancel) {
      sounds_.add(MenuSound::back);
      view_.page = PauseOptionsPage::options;
    }
    return;
  }
  if (view_.page == PauseOptionsPage::costume) {
    if (cancel) {
      sounds_.add(MenuSound::back);
      view_.page = PauseOptionsPage::options;
    } else if (confirm) {
      sounds_.add(MenuSound::confirm);
      const auto costume = static_cast<unsigned>(costume_.selected);
      if (costume == view_.settings.costume) {
        view_.page = PauseOptionsPage::options;
      } else {
        costume_request_ = costume;
      }
    } else if ((input.repeated & kUp) != 0) {
      sounds_.add(MenuSound::cursor);
      costume_.selected = (costume_.selected + kCostumes - 1) % kCostumes;
    } else if ((input.repeated & kDown) != 0) {
      sounds_.add(MenuSound::cursor);
      costume_.selected = (costume_.selected + 1) % kCostumes;
    }
    return;
  }
  if ((input.repeated & kUp) != 0) {
    sounds_.add(MenuSound::cursor);
    view_.selected = static_cast<PauseOption>(
        (static_cast<unsigned>(view_.selected) + kOptions - 1) % kOptions);
  } else if ((input.repeated & kDown) != 0) {
    sounds_.add(MenuSound::cursor);
    view_.selected = static_cast<PauseOption>(
        (static_cast<unsigned>(view_.selected) + 1) % kOptions);
  } else if ((input.repeated & kHorizontal) != 0) {
    change_setting((input.repeated & kLeft) != 0 ? -1 : 1, false);
  } else if ((input.pressed & kReset) != 0) {
    change_setting(0, true);
  } else if (confirm || start) {
    // Start uses the original save/exit route independently of selection.
    if (start || view_.selected == PauseOption::exit) {
      if (!start) {
        sounds_.add(MenuSound::confirm);
      }
      if (!changed()) {
        begin_exit();
      } else if (services_.settings.autosave_enabled()) {
        save();
      } else {
        dialog(PauseOptionsPage::save_prompt, true);
      }
    } else if (view_.selected == PauseOption::controls) {
      sounds_.add(MenuSound::confirm);
      child_transition_ = ChildTransition::entering_controls;
      child_transition_seconds_ = 0;
    } else if (view_.selected == PauseOption::brightness) {
      sounds_.add(MenuSound::confirm);
      view_.page = PauseOptionsPage::brightness;
      view_.elapsed = 0;
      view_.opening = true;
    } else if (view_.selected == PauseOption::costume) {
      sounds_.add(MenuSound::confirm);
      publish_costumes();
      view_.page = PauseOptionsPage::costume;
    }
  } else if (cancel) {
    sounds_.add(MenuSound::back);
    if (changed()) {
      dialog(PauseOptionsPage::discard, false);
    } else {
      begin_exit();
    }
  }
}
void PauseOptionsFlow::complete_costume(bool success) {
  if (!costume_request_) {
    throw std::logic_error("No pending costume transaction");
  }
  if (success) {
    view_.settings.costume = *costume_request_;
  }
  costume_request_.reset();
  view_.page = success ? PauseOptionsPage::options : PauseOptionsPage::notice;
  view_.notice = success
                     ? std::string_view{}
                     : "Costume could not be loaded; current costume retained.";
}
}  // namespace bw::game
