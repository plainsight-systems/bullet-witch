// Engine monotonic sampling and presentation deadlines. Validate each candidate
// before publication, cap only the presentation domain and never invent
// catch-up frames. Rebase excludes an explicit loading transaction. Fixed
// scalar state, no allocation or native clock reads. Design C.9/I.1;
// implementation Core E.4
// (#re-design-invariants) preserves state on rejection; both-phase MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds frame work.
#include "bw/timing/frame_clock.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bw::timing {
namespace {
constexpr float kPresentationLimit = .1F;
void validate_deadline(FrameClock::Clock::time_point now,
                       FrameClock::Clock::duration period) {
  if (period <= FrameClock::Clock::duration::zero() ||
      now > FrameClock::Clock::time_point::max() - period) {
    throw std::invalid_argument("Invalid presentation deadline");
  }
}
}  // namespace
FrameClock::FrameClock(Clock::time_point started, Clock::duration target_period)
    : previous_(started), deadline_(started), period_(target_period) {
  validate_deadline(started, target_period);
}
FrameTiming FrameClock::sample(Clock::time_point now) {
  validate_deadline(now, period_);
  if (now < previous_) {
    throw std::invalid_argument("Monotonic presentation time moved backwards");
  }
  // Convert before subtracting so even extreme valid timestamps cannot overflow
  // the native signed duration representation.
  using Seconds = std::chrono::duration<long double>;
  const auto elapsed = Seconds(now.time_since_epoch()).count() -
                       Seconds(previous_.time_since_epoch()).count();
  const float wall = static_cast<float>(elapsed);
  if (!std::isfinite(wall)) {
    throw std::invalid_argument("Presentation elapsed time is not finite");
  }
  validate_deadline(deadline_, period_);
  auto next = deadline_ + period_;
  if (next <= now) next = now + period_;
  previous_ = now;
  deadline_ = next;
  return {wall, std::min(wall, kPresentationLimit)};
}
void FrameClock::rebase(Clock::time_point now) {
  validate_deadline(now, period_);
  if (now < previous_) {
    throw std::invalid_argument(
        "Monotonic presentation rebase moved backwards");
  }
  previous_ = now;
  deadline_ = now + period_;
}
}  // namespace bw::timing
