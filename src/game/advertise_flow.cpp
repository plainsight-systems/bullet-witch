// G2: opening-cinematic branches recovered at 0x49e610 and 0x49e900.
// Implements advertise_flow.hpp's exclusive borrowed media/failure contract.
// Validate delta before mutation, execute one branch, then add delta to phase
// time. Movie and fade completion are independent observations; ordinary failed
// opens use the recovered three-second timer. Cleanup precedes task removal.
// O(1) work/storage; only exceptional diagnostic assignment may allocate.
// MCP design/implementation: cpp-guidelines I.1 (#ri-explicit) makes timing and
// input explicit; cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) prevents
// duplicated cleanup, including partially failed media acquisitions.
#include "advertise_flow.hpp"

#include <cmath>
#include <stdexcept>

namespace bw::game {
namespace {
void validate_delta(float delta) {
  if (!std::isfinite(delta) || delta < 0) {
    throw std::invalid_argument("Screen delta must be finite and nonnegative");
  }
}
}  // namespace
AdvertiseFlow::~AdvertiseFlow() { stop(); }
void AdvertiseFlow::stop() noexcept {
  if (owns_movie_) {
    media_.get().stop_movie();
    owns_movie_ = false;
  }
}
void AdvertiseFlow::transition(Phase phase) {
  phase_ = phase;
  elapsed_ = 0;
}
void AdvertiseFlow::start() {
  if (progress_.status == FlowStatus::pending) {
    throw std::logic_error("Opening cinematic already active");
  }
  stop();
  progress_ = {FlowStatus::pending, false, 0};
  diagnostic_.clear();
  transition(Phase::reveal);
  try {
    owns_movie_ = true;
    movie_available_ = media_.get().open_movie("data/movie/advertise.ssu");
    if (!movie_available_) {
      stop();
      diagnostic_ =
          "Opening cinematic unavailable; recovered 3-second fallback";
    }
  } catch (const std::exception& error) {
    stop();
    progress_.status = FlowStatus::failed;
    diagnostic_ = error.what();
  }
}
void AdvertiseFlow::update(LogoUpdate input) {
  validate_delta(input.delta);
  if (progress_.status != FlowStatus::pending) {
    return;
  }
  try {
    constexpr std::uint32_t skip_actions = 0x1010U | 0x2020U;
    constexpr float fade_duration = 0.25F;
    constexpr float fallback_duration = 3.0F;  // 0x6c13a4
    switch (phase_) {
      case Phase::reveal:
        progress_.visible = true;
        media_.get().begin_fade(FadeDirection::reveal, fade_duration);
        transition(Phase::revealing);
        break;
      case Phase::revealing:
        if (!media_.get().fade_pending()) {
          transition(Phase::playing);
        }
        break;
      case Phase::playing:
        if ((input.pressed_actions & skip_actions) != 0 ||
            (movie_available_ ? media_.get().movie_complete()
                              : elapsed_ >= fallback_duration)) {
          transition(Phase::conceal);
        }
        break;
      case Phase::conceal:
        media_.get().begin_fade(FadeDirection::conceal, fade_duration);
        transition(Phase::concealing);
        break;
      case Phase::concealing:
        if (!media_.get().fade_pending()) {
          transition(Phase::hide);
        }
        break;
      case Phase::hide:
        progress_.visible = false;
        transition(Phase::finish);
        break;
      case Phase::finish:
        progress_.result = 1;
        stop();
        progress_.status = FlowStatus::complete;
        elapsed_ = 0;
        break;
    }
    elapsed_ += input.delta;
  } catch (const std::exception& error) {
    stop();
    progress_.visible = false;
    progress_.status = FlowStatus::failed;
    diagnostic_ = error.what();
  }
}
}  // namespace bw::game
