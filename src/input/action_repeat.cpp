// The original repeat bank consumes renderer delta rather than sampling wall
// time. Update a bounded candidate bank before publication so malformed timing
// cannot partially change input state. One source threshold event per held bit;
// no catch-up loop. Both-phase I.1 (#ri-explicit) makes elapsed input explicit;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps timers
// inline.
#include "bw/input/action_repeat.hpp"

#include <cmath>
#include <stdexcept>

namespace bw::input {
std::uint32_t ActionRepeat::advance(ActionButtons buttons,
                                    float presentation_seconds) {
  if (!std::isfinite(presentation_seconds) || presentation_seconds < 0) {
    throw std::invalid_argument("Invalid action repeat interval");
  }
  constexpr float kDelay = .5F, kInterval = .125F;
  auto candidate = elapsed_;
  auto repeated = buttons.pressed;
  for (std::size_t bit = 0; bit < candidate.size(); ++bit) {
    const auto mask = std::uint32_t{1} << bit;
    auto& timer = candidate.at(bit);
    if ((buttons.held & mask) == 0) {
      timer = 0;
      continue;
    }
    timer += presentation_seconds;
    if (!std::isfinite(timer)) {
      throw std::invalid_argument("Action repeat timer overflow");
    }
    if (timer >= kDelay) {
      repeated |= mask;
      timer -= kInterval;
    }
  }
  elapsed_ = candidate;
  return repeated;
}
}  // namespace bw::input
