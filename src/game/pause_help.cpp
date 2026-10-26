// Native Help transitions with narrow read-marker authority. No engine clock,
// gameplay or storage calls. Inputs/timing validate before publication; topic
// indices preserve original IDs through filtered lists. Native reading admits
// twelve 40-pixel lines and returns to list on either confirm or cancel. Core
// I.1/C.9 and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md), both phases consulted; fixed storage.
#include "pause_help.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace bw::game {
namespace {
constexpr float kTransitionSeconds = 1.15F, kLineHeight = 40;
constexpr int kPageStep = 5;
constexpr double kListDamping = .075, kScrollDamping = .16, kPanelDamping = .04,
                 kRowSpacing = 100, kPulsePeriod = 2, kHighlightLow = -.2;
constexpr unsigned kVisibleLines = 12, kMaximumLines = 128;
constexpr std::uint32_t kConfirm = 0x1010, kCancel = 0x2020, kUp = 0x01000000,
                        kDown = 0x02000000, kPageUp = 0x10000000,
                        kPageDown = 0x20000000;
}  // namespace
void PauseHelpFlow::start(
    const std::array<unsigned, help_topic_count>& line_counts) {
  for (const auto count : line_counts) {
    if (count == 0 || count > kMaximumLines) {
      throw std::invalid_argument("Invalid Help content line count");
    }
  }
  sounds_ = {};
  const auto progress = control_.progress();
  lines_ = line_counts;
  view_ = {};
  list_motion_ = {};
  scroll_motion_ = {};
  panel_motion_ = {};
  scroll_target_ = pulse_seconds_ = 0;
  view_.phase = PauseHelpPhase::opening;
  view_.read = progress.read;
  for (unsigned topic = 0; topic < help_topic_count; ++topic) {
    if (progress.available.at(topic)) view_.topics.at(view_.count++) = topic;
  }
  select(0);
}
void PauseHelpFlow::select(std::size_t selected) {
  view_.selected = selected;
  view_.scroll_pixels = 0;
  scroll_motion_ = {};
  scroll_target_ = 0;
  const auto lines = lines_.at(view_.topics.at(selected));
  view_.maximum_scroll =
      static_cast<float>(lines > kVisibleLines ? lines - kVisibleLines : 0) *
      kLineHeight;
}
void PauseHelpFlow::advance_visuals(double seconds) {
  // Native Help retains separate list, paragraph and reading-focus channels.
  // Shared damped response preserves the source duration coefficients.
  list_motion_ = math::damped_response(
      list_motion_,
      {-static_cast<double>(view_.selected) * kRowSpacing, kListDamping},
      seconds);
  scroll_motion_ = math::damped_response(
      scroll_motion_, {scroll_target_, kScrollDamping}, seconds);
  panel_motion_ = math::damped_response(
      panel_motion_,
      {view_.phase == PauseHelpPhase::reading ? 1. : 0., kPanelDamping},
      seconds);
  view_.list_offset = static_cast<float>(list_motion_.value);
  view_.scroll_pixels = static_cast<float>(std::clamp(
      scroll_motion_.value, 0., static_cast<double>(view_.maximum_scroll)));
  view_.panel_focus = static_cast<float>(panel_motion_.value);
  constexpr double kReadingRate = 5, kTopicRate = 3.5;
  const auto linear = [seconds](float value, float target, double rate) {
    return static_cast<float>(
        value < target
            ? std::min(static_cast<double>(target), value + rate * seconds)
            : std::max(static_cast<double>(target), value - rate * seconds));
  };
  view_.reading_strength =
      linear(view_.reading_strength,
             view_.phase == PauseHelpPhase::reading ? 1.F : 0.F, kReadingRate);
  for (std::size_t row = 0; row < view_.count; ++row) {
    view_.topic_strength.at(row) =
        linear(view_.topic_strength.at(row), row == view_.selected ? 1.F : 0.F,
               kTopicRate);
  }
  view_.highlight = static_cast<float>(math::periodic_sine_interpolate(
      math::PeriodicSineEndpoints<double>{0, kHighlightLow},
      pulse_seconds_ / kPulsePeriod));
  pulse_seconds_ = std::fmod(pulse_seconds_ + seconds, kPulsePeriod);
}
void PauseHelpFlow::update(MenuInput input, double presentation_delta,
                           float scroll_displacement) {
  if (!std::isfinite(presentation_delta) || presentation_delta < 0 ||
      presentation_delta > std::numeric_limits<float>::max() ||
      !std::isfinite(scroll_displacement)) {
    throw std::invalid_argument("Invalid Help presentation input");
  }
  sounds_ = {};
  if (completed()) return;
  view_.elapsed = static_cast<float>(
      std::min(static_cast<double>(kTransitionSeconds),
               static_cast<double>(view_.elapsed) + presentation_delta));
  if (view_.phase == PauseHelpPhase::opening ||
      view_.phase == PauseHelpPhase::closing) {
    if (view_.elapsed >= kTransitionSeconds) {
      view_.phase = view_.phase == PauseHelpPhase::opening
                        ? PauseHelpPhase::topics
                        : PauseHelpPhase::complete;
    }
    advance_visuals(presentation_delta);
    return;
  }
  if (view_.phase == PauseHelpPhase::reading) {
    scroll_target_ = std::clamp(scroll_target_ - scroll_displacement, 0.,
                                static_cast<double>(view_.maximum_scroll));
    if ((input.pressed & (kConfirm | kCancel)) != 0) {
      sounds_.add(MenuSound::back);
      view_.phase = PauseHelpPhase::topics;
    }
    advance_visuals(presentation_delta);
    return;
  }
  if ((input.pressed & kConfirm) != 0) {
    const auto topic = view_.topics.at(view_.selected);
    control_.mark_read(topic);
    sounds_.add(MenuSound::confirm);
    view_.read.at(topic) = true;
    // Native reading entry reinitializes the paragraph response even when
    // returning to the same topic after its list preview.
    scroll_motion_ = {};
    scroll_target_ = 0;
    view_.scroll_pixels = 0;
    view_.phase = PauseHelpPhase::reading;
  } else if ((input.pressed & kCancel) != 0) {
    sounds_.add(MenuSound::back);
    view_.phase = PauseHelpPhase::closing;
    view_.elapsed = 0;
  } else {
    int direction = 0;
    if ((input.repeated & kUp) != 0) {
      direction = -1;
    } else if ((input.repeated & kDown) != 0) {
      direction = 1;
    } else if ((input.repeated & kPageUp) != 0) {
      direction = -kPageStep;
    } else if ((input.repeated & kPageDown) != 0) {
      direction = kPageStep;
    }
    if (direction != 0) {
      const auto last = static_cast<int>(view_.count - 1);
      const auto selected =
          std::clamp(static_cast<int>(view_.selected) + direction, 0, last);
      if (selected != static_cast<int>(view_.selected)) {
        sounds_.add(MenuSound::cursor);
        select(static_cast<std::size_t>(selected));
      }
    }
  }
  advance_visuals(presentation_delta);
}
}  // namespace bw::game
