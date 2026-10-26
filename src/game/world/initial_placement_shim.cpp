// Implement the explicitly authorized fresh-start compatibility boundary.
// Validate mission/stage and reject checkpoint restoration before returning
// the recovered cm01_00 start() pose. This does not run or satisfy startup.
// Fixed value work, no allocations or retained input. Both-phase I.1
// (#ri-explicit) and MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) apply as in the header.
#include "initial_placement_shim.hpp"

#include <stdexcept>

namespace bw::game {
ScriptedPlayerPose initial_placement_shim(std::string_view mission,
                                          std::string_view stage,
                                          bool restoring_checkpoint) {
  if (mission != "cm01_00" || stage != "st04_c" || restoring_checkpoint) {
    throw std::invalid_argument(
        "Initial placement shim supports only fresh cm01_00/st04_c");
  }
  // Only the approved fresh-start pose lives here; checkpoint branches must
  // eventually execute through the genuine mission/script command owner.
  constexpr ScriptedPlayerPose kFreshOneShotKillPose{{-61.91F, 0.65F, -97.57F},
                                                     -0.28F};
  return kFreshOneShotKillPose;
}
}  // namespace bw::game
