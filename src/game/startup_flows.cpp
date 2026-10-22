// G2: execute the local update branches recovered at 0x49e320 / 0x4a0f20.
// Owns phase/index/clocks; borrows media under startup_flows.hpp's contract.
// Each call executes one branch then advances clocks, including transition
// calls. Invalid deltas throw before mutation; media exceptions stop playback
// and expose failed status. Native movie-open failure continuation remains
// unresolved and explicitly fails. Destructor stops owned playback without
// pretending completion. Algorithm/cost: bounded switch, O(1) control work, no
// normal tick allocation; paths are moved once at construction.
// Renderer/session side effects remain external and are not inferred from
// successful update-policy tests. MCP design/implementation: cpp-guidelines I.1
// (#ri-explicit) makes media failures and delta explicit; cpp-performance
// COPY.5 (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// requires exclusive cleanup ownership, so this media operation cannot be
// copied/moved.
#include "startup_flows.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace bw::game {
namespace {
void valid_delta(float delta) {
  if (!std::isfinite(delta) || delta < 0) {
    throw std::invalid_argument("Startup delta must be finite and nonnegative");
  }
}
}  // namespace
CompanyLogoFlow::CompanyLogoFlow(LogoMedia& media,
                                 std::vector<std::string> movie_paths)
    : media_(media), paths_(std::move(movie_paths)) {
  if (paths_.empty()) {
    throw std::invalid_argument("Company logo playlist is required");
  }
  for (const auto& path : paths_) {
    if (path.empty()) {
      throw std::invalid_argument("Company logo path is empty");
    }
  }
}
CompanyLogoFlow::~CompanyLogoFlow() { stop(); }
void CompanyLogoFlow::stop() noexcept {
  if (owns_movie_) {
    media_.get().stop_movie();
    owns_movie_ = false;
  }
}
void CompanyLogoFlow::transition(Phase phase) {
  phase_ = phase;
  elapsed_ = 0;
}
void CompanyLogoFlow::start() {
  if (progress_.status == FlowStatus::pending) {
    throw std::logic_error("Company logo flow already active");
  }
  stop();
  progress_ = {FlowStatus::pending, false, 0};
  diagnostic_.clear();
  index_ = 0;
  transition(Phase::initialize);
}
void CompanyLogoFlow::update(LogoUpdate input) {
  const auto delta = input.delta;
  valid_delta(delta);
  if (progress_.status != FlowStatus::pending) {
    return;
  }
  try {
    constexpr std::uint32_t skip_actions = 0x1010U | 0x2020U;
    constexpr float fade_duration = 0.25F;  // native 0x3e800000
    switch (phase_) {
      case Phase::initialize:
        transition(Phase::open);
        break;
      case Phase::open:
        stop();
        // An attempted open can acquire partial resources before failing.
        owns_movie_ = true;
        if (media_.get().open_movie(paths_.at(index_))) {
          media_.get().begin_fade(FadeDirection::reveal, fade_duration);
          progress_.visible = true;
          transition(Phase::revealing);
        } else {
          stop();
          progress_.visible = false;
          // Failure increments native state 500 -> 501, not 8000 -> 8001.
          // The recovered failure continuation is therefore unresolved.
          progress_.status = FlowStatus::failed;
          diagnostic_ = "Movie open failed; native continuation unresolved";
        }
        break;
      case Phase::revealing:
        if (!media_.get().fade_pending()) {
          transition(Phase::playing);
        }
        break;
      case Phase::playing:
        if (media_.get().movie_complete() ||
            (input.pressed_actions & skip_actions) != 0) {
          transition(Phase::conceal);
        }
        break;
      case Phase::conceal:
        media_.get().begin_fade(FadeDirection::conceal, fade_duration);
        transition(Phase::concealing);
        break;
      case Phase::concealing:
        if (!media_.get().fade_pending()) {
          ++index_;
          transition(index_ < paths_.size() ? Phase::open : Phase::hide);
        }
        break;
      case Phase::hide:
        progress_.visible = false;
        transition(Phase::finish);
        break;
      case Phase::finish:
        elapsed_ = 0;
        stop();
        progress_.result = 1;
        progress_.status = FlowStatus::complete;
        break;
    }
    elapsed_ += delta;
  } catch (const std::exception& failure) {
    stop();
    progress_.visible = false;
    progress_.status = FlowStatus::failed;
    diagnostic_ = failure.what();
  }
}
void StartupCheckFlow::start() {
  if (progress_.status == FlowStatus::pending) {
    throw std::logic_error("Startup check flow already active");
  }
  progress_ = {FlowStatus::pending, false, 0};
  phase_ = Phase::show;
  ui_phase_ = CheckUiPhase::reset;
  elapsed_ = 0;
  ui_elapsed_ = 0;
}
void StartupCheckFlow::update(float delta) {
  valid_delta(delta);
  if (progress_.status != FlowStatus::pending) {
    return;
  }
  switch (phase_) {
    case Phase::show:
      progress_.visible = true;
      ui_phase_ = CheckUiPhase::reveal;
      ui_elapsed_ = 0;
      phase_ = Phase::hold;
      break;
    case Phase::hold:
      ui_phase_ = CheckUiPhase::hold;
      ui_elapsed_ = 0;
      phase_ = Phase::close;
      break;
    case Phase::close:
      phase_ = Phase::begin_close;
      break;
    case Phase::begin_close:
      ui_phase_ = CheckUiPhase::close;
      ui_elapsed_ = 0;
      phase_ = Phase::closing;
      break;
    case Phase::closing:
      phase_ = Phase::hide;
      break;
    case Phase::hide:
      progress_.visible = false;
      phase_ = Phase::finish;
      break;
    case Phase::finish:
      progress_.result = 1;
      progress_.status = FlowStatus::complete;
      break;
  }
  // Every mapped branch changes the primary state and resets its clock.
  elapsed_ = delta;
  ui_elapsed_ += delta;
}
}  // namespace bw::game
