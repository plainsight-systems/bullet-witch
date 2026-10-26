// Publish one source world interval before choosing normal/control work. Scale
// changes affect future frames; retained frame values are immutable
// observations. No worker, allocation, device query, fixed-step accumulation or
// second clamp. Design C.9/I.1; implementation E.4 (#re-design-invariants)
// validates every candidate before committing sequence/time; both-phase
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) uses
// fixed scalar state.
#include "bw/timing/world_clock.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace bw::world {
WorldFrame::WorldFrame(const WorldClock& owner, float delta,
                       TickAdmission admission)
    : owner_(&owner),
      sequence_(owner.sequence_),
      delta_(delta),
      simulation_seconds_(owner.simulation_seconds()),
      admission_(admission) {}
void WorldClock::set_scale(float scale) {
  if (!std::isfinite(scale) || scale < 0) {
    throw std::invalid_argument("Invalid world time scale");
  }
  scale_ = scale;
}
WorldFrame WorldClock::publish(float presentation_seconds,
                               TickAdmission admission) {
  const float delta = presentation_seconds * scale_;
  const double next = simulation_seconds_ +
                      (admission.simulation ? static_cast<double>(delta) : 0);
  if (!std::isfinite(presentation_seconds) || presentation_seconds < 0 ||
      !std::isfinite(delta) || !std::isfinite(next) ||
      sequence_ == std::numeric_limits<std::uint64_t>::max()) {
    throw std::invalid_argument("Invalid world frame publication");
  }
  ++sequence_;
  simulation_seconds_ = next;
  return {*this, delta, admission};
}
}  // namespace bw::world
