// Original logical menu events resolve here, not in native audio or sprites.
// A bounded queue advances from presentation deltas while world audio remains
// paused. Validate complete batches before publishing work. Core I.1
// (#ri-explicit), performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md), both implementation corpora consulted.
#include "menu_audio.hpp"

#include <cmath>
#include <stdexcept>

namespace bw::game {
namespace {
// Compatibility boundary: source event table 0x8367b0, common bank 0.
constexpr std::uint16_t kPauseCue = 12, kConfirmCue = 362, kCursorCue = 363,
                        kErrorCue = 5, kSaveCue = 11, kTitleStartCue = 8,
                        kMissionStartCue = 10, kUpgradePurchaseCue = 17;
std::uint16_t cue(MenuSound sound) {
  switch (sound) {
    case MenuSound::pause:
      return kPauseCue;
    case MenuSound::panel:
      return 1;
    case MenuSound::confirm:
      return kConfirmCue;
    case MenuSound::back:
      return 0;
    case MenuSound::cursor:
      return kCursorCue;
    case MenuSound::error:
      return kErrorCue;
    case MenuSound::dialog_open:
      return 2;
    case MenuSound::dialog_close:
      return 4;
    case MenuSound::save:
      return kSaveCue;
    case MenuSound::title_start:
      return kTitleStartCue;
    case MenuSound::mission_start:
      return kMissionStartCue;
    case MenuSound::upgrade_purchase:
      return kUpgradePurchaseCue;
  }
  throw std::invalid_argument("Invalid menu sound");
}
void validate(const MenuSoundRequest& request) {
  (void)cue(request.sound);
  if (!std::isfinite(request.delay_seconds) || request.delay_seconds < 0) {
    throw std::invalid_argument("Invalid menu sound delay");
  }
}
}  // namespace
void MenuAudio::play(MenuSound sound) {
  (void)output_.play({0, cue(sound), {}, audio::PauseDomain::presentation});
}
void MenuAudio::submit(const MenuSoundRequests& requests) {
  if (requests.size > requests.requests.size()) {
    throw std::invalid_argument("Invalid menu sound batch size");
  }
  std::size_t delayed = 0;
  for (std::size_t i = 0; i < requests.size; ++i) {
    const auto& request = requests.requests.at(i);
    validate(request);
    if (request.delay_seconds > 0) ++delayed;
  }
  if (delayed > pending_.size() - size_) {
    throw std::length_error("Menu sound scheduling capacity exceeded");
  }
  // Immediate cues and delayed cues retain batch order. Only timing and queue
  // capacity errors are transactional; backend/device failures remain fatal.
  for (std::size_t i = 0; i < requests.size; ++i) {
    const auto& request = requests.requests.at(i);
    if (request.delay_seconds == 0) {
      play(request.sound);
    } else {
      pending_.at(size_++) = {request.sound, request.delay_seconds};
    }
  }
}
void MenuAudio::advance(float presentation_seconds) {
  if (!std::isfinite(presentation_seconds) || presentation_seconds < 0) {
    throw std::invalid_argument("Invalid menu audio presentation delta");
  }
  // Compact survivors in place; all due requests emit once in admission order.
  std::size_t retained = 0;
  for (std::size_t i = 0; i < size_; ++i) {
    auto request = pending_.at(i);
    request.remaining_seconds -= presentation_seconds;
    if (request.remaining_seconds <= 0) {
      play(request.sound);
    } else {
      pending_.at(retained++) = request;
    }
  }
  size_ = retained;
}
}  // namespace bw::game
