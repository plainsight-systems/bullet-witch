// Mission lifetime requests are copied values, not authority to reset actors.
// Composition validates before teardown, destroys all mission borrowers, then
// acknowledges the transition to the session. Restart/checkpoint keep the root
// mission wait pending; retirement alone releases it. Main-thread only, no
// allocation. Reentry carries the already-committed live costume copied before
// teardown; it must not be reconstructed from an older Profile selection.
// This session-only appearance handoff grants no Profile/save authority.
// Design Core I.1 (#ri-explicit) makes the lifetime event explicit;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keeps publication inline. See native-pause/mission-reentry.md.
#pragma once
#include <cstdint>

namespace bw::game {
enum class MissionTransition : std::uint8_t {
  none,
  retire,
  restart_stage,
  checkpoint
};
struct MissionTransitionRequest {
  MissionTransition operation{MissionTransition::none};
  // Observed from the committed mission resource owner; ignored on retirement.
  unsigned costume{};
};
}  // namespace bw::game
