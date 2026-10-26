// Explicit absent-provider observation takes the original failure branch only
// after authored entry. Shared acknowledgment owns its input gate and cues;
// leaving waits for the source conceal before releasing the child. I.1 and
// MEM.9 were reread for implementation: supplied time is validated before
// mutation and no update allocates, requests network work or invents results.
#include "ranking_flow.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace bw::game {
namespace {
constexpr float kEntrySeconds = 1.8F, kScreenSeconds = .25F, kPanelDelay = .4F;
}
void RankingFlow::start(RankingAvailability availability) {
  if (availability != RankingAvailability::unavailable) {
    throw std::invalid_argument("Unsupported ranking provider observation");
  }
  if (!completed()) throw std::logic_error("Ranking child already active");
  view_ = {};
  view_.phase = RankingPhase::entering;
  sounds_ = {};
  sounds_.add(MenuSound::panel, kPanelDelay);
}
MenuSoundRequests RankingFlow::take_sounds() {
  return std::exchange(sounds_, {});
}
void RankingFlow::update(MenuInput input, float seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid ranking presentation time");
  }
  sounds_ = {};
  switch (view_.phase) {
    case RankingPhase::complete:
      return;
    case RankingPhase::entering:
      view_.elapsed = std::min(kEntrySeconds, view_.elapsed + seconds);
      view_.opacity = std::min(1.F, view_.elapsed / kScreenSeconds);
      if (view_.elapsed >= kEntrySeconds) {
        view_.phase = RankingPhase::unavailable;
        dialog_.start(false, MenuDialogMode::acknowledgment);
        sounds_ = dialog_.take_sounds();
      }
      return;
    case RankingPhase::unavailable:
      dialog_.update(input, seconds);
      view_.dialog_progress = dialog_.presentation().progress;
      sounds_ = dialog_.take_sounds();
      if (dialog_.presentation().complete) {
        view_.phase = RankingPhase::concealing;
        view_.elapsed = 0;
      }
      return;
    case RankingPhase::concealing:
      view_.elapsed = std::min(kScreenSeconds, view_.elapsed + seconds);
      view_.opacity = 1 - view_.elapsed / kScreenSeconds;
      if (view_.elapsed >= kScreenSeconds) view_.phase = RankingPhase::complete;
      return;
  }
}
}  // namespace bw::game
