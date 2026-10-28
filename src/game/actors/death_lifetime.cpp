// Preserve original death-state delay and camera-relative retirement gates.
// Core I.11/E.4 and performance MEM.9 were reread for implementation: copied
// observations and validated bounded state never acquire destruction authority.
#include "death_lifetime.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bw::game {
void DeathLifetime::advance(float seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid death-state interval");
  }
  remaining_seconds_ = std::max(0.F, remaining_seconds_ - seconds);
}
bool DeathLifetime::retirement_due(
    const DeathRetirementObservation& observation) const {
  if (!math::all_finite(observation.position) ||
      !math::all_finite(observation.camera_position) ||
      !math::all_finite(observation.camera_forward)) {
    throw std::invalid_argument("Invalid corpse retirement observation");
  }
  if (remaining_seconds_ > 0 || observation.camera_owned) return false;
  const auto relative =
      math::subtract(observation.position, observation.camera_position);
  if (math::dot(relative, observation.camera_forward) >= 0) return false;
  constexpr std::uint32_t kExtendedRetentionFamily = 0x100;
  constexpr float kOrdinaryDistance = 5, kExtendedDistance = 40;
  const auto distance =
      (observation.source_family_flags & kExtendedRetentionFamily) != 0
          ? kExtendedDistance
          : kOrdinaryDistance;
  return math::length(relative) > distance;
}
}  // namespace bw::game
