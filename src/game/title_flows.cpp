// G2: original title phase transitions and title-only animation math.
// Implements title_flows.hpp's single-threaded ownership and failure contract.
// Sources: native 0x499f10, 0x5724a0 and 0x572660; constants were
// decoded from the pinned executable, not fitted to playback observations.
// Algorithm: validate delta before mutation, run one branch, then animate and
// advance phase time. A title readiness wait returns before clock advancement.
// Result publication occurs in cleanup, with removal requested next update.
// Title animation uses the recovered periodic sine and bounded linear ramps;
// it does not replace the engine media clock. O(1) work/storage, no normal tick
// allocation. MCP design/implementation: cpp-guidelines I.1 (#ri-explicit)
// keeps media/readiness explicit; cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) ensures
// one owner stops each acquired operation, including partial failed opens.
#include "title_flows.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bw::game {
namespace {
void validate_delta(float delta) {
  if (!std::isfinite(delta) || delta < 0) {
    throw std::invalid_argument("Screen delta must be finite and nonnegative");
  }
}
float approach(float value, float target, float step) {
  return value < target ? std::min(target, value + step)
                        : std::max(target, value - step);
}
}  // namespace
TitleFlow::~TitleFlow() { stop(); }
void TitleFlow::stop() noexcept {
  if (owns_movie_) {
    media_.get().stop_movie();
    owns_movie_ = false;
  }
}
void TitleFlow::transition(Phase phase) {
  phase_ = phase;
  elapsed_ = 0;
}
void TitleFlow::start() {
  if (progress_.status == FlowStatus::pending) {
    throw std::logic_error("Title already active");
  }
  stop();
  progress_ = {FlowStatus::pending, false, 0};
  diagnostic_.clear();
  transition(Phase::initialize);
  common_opacity_ = 1;
  common_target_ = 1;
  detail_opacity_ = 0;
  detail_target_ = 0;
  prompt_ = {};
  prompt_highlight_ = {};
  pending_result_ = 0;
  try {
    owns_movie_ = true;
    movie_available_ = media_.get().open_movie("data/movie/witchLOGO.ssu");
    if (!movie_available_) {
      stop();
      diagnostic_ = "Title movie unavailable; recovered 8-second fallback";
    }
  } catch (const std::exception& error) {
    stop();
    progress_.status = FlowStatus::failed;
    diagnostic_ = error.what();
  }
}
void TitleFlow::Pulse::update(float delta) {
  if (paused) {
    return;
  }
  constexpr float two_pi = 6.2831854820251465F;  // 0x6c1d98
  constexpr float half_range = 0.5F;
  const float fraction = std::clamp(elapsed, 0.0F, duration) / duration;
  value =
      constant ? 1.0F : half_range + half_range * std::sin(fraction * two_pi);
  elapsed += delta;
  if (elapsed >= duration) {
    elapsed = std::fmod(elapsed, duration);
  }
}
void TitleFlow::animate(float delta) {
  constexpr float common_speed = 2.0F;
  common_opacity_ =
      approach(common_opacity_, common_target_, common_speed * delta);
  prompt_.update(delta);
  prompt_highlight_.update(delta);
  detail_opacity_ = approach(detail_opacity_, detail_target_, delta);
}
TitlePresentation TitleFlow::presentation() const {
  const float detail = detail_opacity_ * common_opacity_;
  return {{{prompt_highlight_.value * common_opacity_,
            prompt_.value * common_opacity_, detail}},
          detail};
}
void TitleFlow::update(LogoUpdate input, bool resources_ready) {
  validate_delta(input.delta);
  if (progress_.status != FlowStatus::pending) {
    return;
  }
  try {
    constexpr std::uint32_t accept_actions = 0x1010U;
    constexpr float accepted_pulse_period = 0.1F;
    constexpr float normal_pulse_period = 1.3F;
    constexpr float initial_pulse_time = 0.9749999642372131F;  // 0x3f799999
    constexpr float reveal_duration = 1.0F;
    constexpr float conceal_duration = 0.5F;
    constexpr float movie_timeout = 16.5F;       // 0x95a6a4
    constexpr float fallback_timeout = 8.0F;     // 0x6c1ff8
    constexpr float accept_dimming_time = 0.6F;  // 0x82cfa0
    constexpr float accept_fade_time = 0.9F;     // 0x6c1d88
    switch (phase_) {
      case Phase::initialize:
        if (!resources_ready) {
          return;
        }
        transition(Phase::reveal);
        break;
      case Phase::reveal:
        progress_.visible = true;
        prompt_.elapsed = initial_pulse_time;
        prompt_highlight_.elapsed = initial_pulse_time;
        media_.get().begin_fade(FadeDirection::reveal, reveal_duration);
        transition(Phase::revealing);
        break;
      case Phase::revealing:
        if (!media_.get().fade_pending()) {
          detail_target_ = 1;
          prompt_.paused = false;
          prompt_highlight_.paused = false;
          transition(Phase::waiting);
        }
        break;
      case Phase::waiting:
        if ((input.pressed_actions & accept_actions) != 0) {
          transition(Phase::accept);
        } else if (elapsed_ >=
                   (movie_available_ ? movie_timeout : fallback_timeout)) {
          transition(Phase::timeout);
        }
        break;
      case Phase::accept:
        prompt_ = {0, accepted_pulse_period, 0, false, false};
        prompt_highlight_ = {0, normal_pulse_period, 0, false, true};
        transition(Phase::accepting);
        break;
      case Phase::accepting:
        if (elapsed_ >= accept_dimming_time) {
          common_target_ = 0;
        }
        if (elapsed_ >= accept_fade_time) {
          media_.get().begin_fade(FadeDirection::conceal, conceal_duration);
          transition(Phase::accept_fade);
        }
        break;
      case Phase::accept_fade:
        if (!media_.get().fade_pending()) {
          transition(Phase::accepted);
        }
        break;
      case Phase::accepted:
        pending_result_ = 1;
        transition(Phase::accepted_wait);
        break;
      case Phase::accepted_wait:
        transition(Phase::accepted_route);
        break;
      case Phase::accepted_route:
        transition(Phase::cleanup);
        break;
      case Phase::timeout:
        common_target_ = 0;
        transition(Phase::timeout_movie);
        break;
      case Phase::timeout_movie:
        if (!movie_available_ || media_.get().movie_complete()) {
          media_.get().begin_fade(FadeDirection::conceal,
                                  movie_available_ ? 0.0F : conceal_duration);
          transition(Phase::timeout_fade);
        }
        break;
      case Phase::timeout_fade:
        if (!media_.get().fade_pending()) {
          pending_result_ = 2;
          transition(Phase::cleanup);
        }
        break;
      case Phase::cleanup:
        progress_.visible = false;
        stop();
        progress_.result = pending_result_;
        transition(Phase::finish);
        break;
      case Phase::finish:
        progress_.status = FlowStatus::complete;
        break;
    }
    animate(input.delta);
    elapsed_ += input.delta;
  } catch (const std::exception& error) {
    stop();
    progress_.visible = false;
    progress_.status = FlowStatus::failed;
    diagnostic_ = error.what();
  }
}
}  // namespace bw::game
