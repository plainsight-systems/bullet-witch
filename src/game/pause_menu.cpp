// Apply the native pause task's bounded state transitions and authored flight
// paths. Supplied presentation seconds only; MissionControl retains all pause
// authority. Typed child/retirement requests prevent row IDs from escaping into
// application composition. Unavailable operations never publish requests.
// Implementation consultation: Core E.4 (#re-design-invariants) validates time
// before mutation; performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps frame state inline. Both corpora
// queried and rules read separately from header design. Source map and lifetime
// contract: native-pause-menu.md and pause_menu.hpp.
#include "pause_menu.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/math/scalar.hpp"

namespace bw::game {
namespace {
constexpr std::uint32_t kConfirm = 0x1010, kCancel = 0x2020, kUp = 0x01000000,
                        kDown = 0x02000000;
constexpr float kOpeningSeconds = 1.4F, kClosingSeconds = .6F,
                kHoverStartSeconds = .98F;
struct Flight {
  float start{}, end{}, x{}, y{};
};
constexpr std::array<Flight, 6> kEntry{{{.2F, 1, -540, -240},
                                        {.59F, 1.25F, -600, 300},
                                        {.3F, 1.07F, -560, -260},
                                        {.77F, 1.37F, -700, 200},
                                        {.5F, 1.15F, -600, -300},
                                        {.65F, 1.27F, -600, 200}}};
constexpr std::array<Flight, 6> kExit{{{.05F, .35F, -540, 0},
                                       {.14F, .3F, -520, 0},
                                       {.07F, .37F, -530, 0},
                                       {.12F, .43F, -550, 0},
                                       {.1F, .27F, -545, 0},
                                       {.03F, .43F, -560, 0}}};
constexpr std::array<std::array<float, 2>, 6> kPositions{
    {{228, 116}, {176, 167}, {147, 217}, {141, 273}, {172, 323}, {164, 376}}};
constexpr std::array<std::array<float, 2>, 4> kBackgroundEntry{
    {{.05F, .9F}, {0, .8F}, {.1F, 1}, {.17F, 1.16F}}};
constexpr std::array<std::array<float, 2>, 4> kBackgroundExit{
    {{.1F, .5F}, {.17F, .58F}, {.05F, .45F}, {0, .4F}}};
float interval(float elapsed, float begin, float end) {
  return std::clamp((elapsed - begin) / (end - begin), 0.0F, 1.0F);
}
}  // namespace
PauseMenuFlow::PauseMenuFlow(UserPauseControl control,
                             PauseOperationAvailability availability)
    : control_(control), availability_(availability) {
  publish();
}
void PauseMenuFlow::enter(PauseMenuPhase phase) {
  phase_ = phase;
  elapsed_ = 0;
}
void PauseMenuFlow::activate_selection() {
  if (selected_ == PauseMenuAction::resume) {
    enter(PauseMenuPhase::closing);
  } else if (selected_ == PauseMenuAction::help ||
             selected_ == PauseMenuAction::options) {
    request_ = selected_;
    enter(PauseMenuPhase::child);
  } else {
    const bool available = selected_ == PauseMenuAction::checkpoint
                               ? availability_.checkpoint
                               : (selected_ == PauseMenuAction::restart_stage
                                      ? availability_.restart_stage
                                      : availability_.retire);
    confirmation_yes_ = false;
    dialog_closing_ = false;
    dialog_progress_ = 0;
    if (available) {
      dialog_.start(false);
      (void)dialog_.take_sounds();
    }
    sounds_.add(MenuSound::dialog_open);
    enter(available ? PauseMenuPhase::confirmation
                    : PauseMenuPhase::unavailable);
  }
}
bool PauseMenuFlow::update(float presentation_seconds, bool pause_pressed,
                           MenuInput input) {
  if (!std::isfinite(presentation_seconds) || presentation_seconds < 0) {
    throw std::invalid_argument("Invalid pause presentation interval");
  }
  sounds_ = {};
  if (phase_ == PauseMenuPhase::inactive) {
    if (!pause_pressed || !control_.set_user_paused(true)) {
      return false;
    }
    selected_ = PauseMenuAction::help;
    item_elapsed_.fill(0);
    request_.reset();
    enter(PauseMenuPhase::opening);
    constexpr float kPanelSoundDelay = .4F;
    sounds_.add(MenuSound::pause);
    sounds_.add(MenuSound::panel, kPanelSoundDelay);
    publish();
    return true;
  }
  if (!control_.user_paused()) {
    throw std::logic_error("Active pause task lost user-pause authority");
  }
  // The caller dispatches child input; the normal root cannot consume the same
  // confirmation twice or resume underneath a child resource transaction.
  if (phase_ == PauseMenuPhase::child || phase_ == PauseMenuPhase::retiring) {
    return true;
  }
  const bool confirm = (input.pressed & kConfirm) != 0;
  const bool cancel = (input.pressed & kCancel) != 0;
  if (pause_pressed && phase_ != PauseMenuPhase::closing) {
    sounds_.add(MenuSound::back);
    enter(PauseMenuPhase::closing);
  } else if (phase_ == PauseMenuPhase::confirmation) {
    dialog_.update(input, presentation_seconds);
    const auto batch = dialog_.take_sounds();
    for (std::size_t i = 0; i < batch.size; ++i) {
      sounds_.add(batch.requests.at(i).sound);
    }
    const auto state = dialog_.presentation();
    confirmation_yes_ = state.yes;
    dialog_progress_ = state.progress;
    if (state.complete) {
      if (state.yes) {
        request_ = selected_;
        enter(PauseMenuPhase::retiring);
      } else {
        enter(PauseMenuPhase::interactive);
      }
    }
  } else if (phase_ == PauseMenuPhase::unavailable) {
    constexpr float kInputDelay = .3F, kDialogRate = 4.4F,
                    kDialogTolerance = .01F;
    if (dialog_closing_) {
      dialog_progress_ =
          std::max(0.F, dialog_progress_ - kDialogRate * presentation_seconds);
      if (dialog_progress_ < kDialogTolerance) {
        enter(PauseMenuPhase::interactive);
      }
    } else {
      if (elapsed_ >= kInputDelay &&
          dialog_progress_ > 1.F - kDialogTolerance) {
        if (confirm || cancel) {
          sounds_.add(confirm ? MenuSound::confirm : MenuSound::back);
          sounds_.add(MenuSound::dialog_close);
          dialog_closing_ = true;
        }
      }
      dialog_progress_ =
          std::min(1.F, dialog_progress_ + kDialogRate * presentation_seconds);
    }
  } else if (phase_ == PauseMenuPhase::interactive) {
    if (confirm) {
      sounds_.add(MenuSound::confirm);
      activate_selection();
    } else if (cancel) {
      sounds_.add(MenuSound::back);
      if (selected_ == PauseMenuAction::resume) {
        enter(PauseMenuPhase::closing);
      } else {
        selected_ = PauseMenuAction::resume;
      }
    }
  }
  if (phase_ == PauseMenuPhase::opening ||
      phase_ == PauseMenuPhase::interactive) {
    auto row = static_cast<unsigned>(selected_);
    constexpr unsigned kRows = 6;
    if ((input.repeated & kUp) != 0) {
      row = (row + kRows - 1) % kRows;
    } else if ((input.repeated & kDown) != 0) {
      row = (row + 1) % kRows;
    }
    if (row != static_cast<unsigned>(selected_)) {
      sounds_.add(MenuSound::cursor);
    }
    constexpr std::array<PauseMenuAction, kRows> kActions{
        PauseMenuAction::help,       PauseMenuAction::options,
        PauseMenuAction::checkpoint, PauseMenuAction::restart_stage,
        PauseMenuAction::retire,     PauseMenuAction::resume};
    selected_ = kActions.at(row);
  }
  elapsed_ += presentation_seconds;
  constexpr float kHoverPeriod = 1.8F;
  for (auto& elapsed : item_elapsed_) {
    elapsed = std::fmod(elapsed + presentation_seconds, kHoverPeriod);
  }
  if (phase_ == PauseMenuPhase::opening && elapsed_ > kOpeningSeconds) {
    enter(PauseMenuPhase::interactive);
  } else if (phase_ == PauseMenuPhase::closing && elapsed_ > kClosingSeconds) {
    (void)control_.set_user_paused(false);
    enter(PauseMenuPhase::inactive);
  }
  publish();
  return true;
}
void PauseMenuFlow::complete_child() {
  if (phase_ != PauseMenuPhase::child || !request_ ||
      (*request_ != PauseMenuAction::help &&
       *request_ != PauseMenuAction::options)) {
    throw std::logic_error("Pause child completion has no active child");
  }
  request_.reset();
  enter(PauseMenuPhase::interactive);
  publish();
}
void PauseMenuFlow::publish() {
  output_ = {};
  output_.phase = phase_;
  output_.selected = selected_;
  output_.confirmation_yes = confirmation_yes_;
  if (phase_ == PauseMenuPhase::inactive) {
    return;
  }
  constexpr float kBackgroundStart = -700;
  const bool opening = phase_ == PauseMenuPhase::opening;
  const bool closing = phase_ == PauseMenuPhase::closing;
  constexpr float kShadeSeconds = .35F, kShadeAlpha = 127.F / 255.F;
  output_.shade_alpha = kShadeAlpha;
  output_.background_alpha.fill(1);
  output_.dialog_progress = dialog_progress_;
  if (opening) {
    output_.shade_alpha *=
        math::quintic_ease(interval(elapsed_, 0, kShadeSeconds));
  } else if (closing) {
    output_.shade_alpha *=
        1 - math::quintic_ease(interval(elapsed_, 0, kClosingSeconds));
  }
  for (std::size_t i = 0; i < output_.background_x.size(); ++i) {
    const auto times = opening ? kBackgroundEntry.at(i) : kBackgroundExit.at(i);
    const auto factor =
        math::quintic_ease(interval(elapsed_, times[0], times[1]));
    output_.background_x.at(i) =
        opening ? kBackgroundStart * (1 - factor)
                : (closing ? kBackgroundStart * factor : 0);
  }
  for (std::size_t i = 0; i < output_.items.size(); ++i) {
    auto& item = output_.items.at(i);
    const auto position = kPositions.at(i);
    item = {.x = position[0],
            .y = position[1],
            .scale = 1,
            .alpha = 1,
            .brightness = 1};
    if (opening) {
      const auto flight = kEntry.at(i);
      const auto remaining = 1 - interval(elapsed_, flight.start, flight.end);
      const auto x = flight.x * remaining;
      const auto y = flight.y * remaining;
      constexpr float kDistanceLimit = 160000, kFlightScale = 2.5F;
      const auto distance =
          std::min(x * x + y * y, kDistanceLimit) / kDistanceLimit;
      item.x += x;
      item.y += y;
      item.scale = 1 + distance * kFlightScale;
      item.alpha = 1 - distance * distance * distance;
    } else if (closing) {
      const auto flight = kExit.at(i);
      item.x += flight.x * math::quintic_ease(
                               interval(elapsed_, flight.start, flight.end));
      constexpr float kRowExitSeconds = .21F;
      item.alpha =
          1 - math::quintic_ease(interval(elapsed_, 0, kRowExitSeconds));
    }
    if (i != static_cast<std::size_t>(selected_)) {
      constexpr float kDim = .6F, kHalf = .5F;
      const auto brightness =
          1 - std::min(std::abs(1 - item.scale), 2.0F) * kHalf;
      item.brightness = 1 - kDim * brightness * brightness * brightness;
    } else if (!opening || elapsed_ > kHoverStartSeconds) {
      constexpr float kDegreesPerSecond = 200, kDegrees = 180, kHoverPixels = 4;
      item.y += std::sin(item_elapsed_.at(i) * kDegreesPerSecond *
                         std::numbers::pi_v<float> / kDegrees) *
                kHoverPixels;
    }
  }
  if (phase_ == PauseMenuPhase::unavailable) {
    output_.message = selected_ == PauseMenuAction::checkpoint
                          ? "Checkpoint restoration is not available yet."
                          : (selected_ == PauseMenuAction::restart_stage
                                 ? "Stage restart is not available yet."
                                 : "Mission retirement is not available yet.");
  }
}
}  // namespace bw::game
