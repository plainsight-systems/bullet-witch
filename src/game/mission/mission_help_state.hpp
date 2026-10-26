// Mission Help observes the live permanent learned/read flags through a narrow
// Profile capability. The explicit development-completed concept profile adds
// source-derived campaign grants and acquired-ability topics without changing
// permanent unlocks. Reads of genuinely learned topics go to Profile; reads of
// inherited-only topics stay in this mission. Ordinary saved mode is unchanged.
// Stable owner and const player borrower live through PauseHelpFlow teardown;
// no settings, persistence, unlock or mutable player authority is exposed.
// Algorithm: copy live saved progress -> merge admitted entry context -> merge
// mission-local read bits; checked read command chooses the owning store.
// Fixed 33-topic storage, O(33) observation/O(1) write, no steady-state
// allocation. Design Core I.1 (#ri-explicit), C.9 (#rc-private) and performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) make
// context/lifetimes explicit and keep bounded observations. Source:
// native-pause-help.md.
#pragma once
#include <array>

#include "game/profile/profile.hpp"
#include "mission_player_state.hpp"

namespace bw::game {
class MissionHelpState {
 public:
  explicit MissionHelpState(HelpControl saved) : saved_(saved) {}
  MissionHelpState(HelpControl saved, const MissionPlayerState& player)
      : saved_(saved), player_(&player) {}
  ~MissionHelpState() = default;
  MissionHelpState(const MissionHelpState&) = delete;
  MissionHelpState& operator=(const MissionHelpState&) = delete;
  MissionHelpState(MissionHelpState&&) = delete;
  MissionHelpState& operator=(MissionHelpState&&) = delete;
  HelpProgress progress() const;
  void mark_read(unsigned topic);

 private:
  HelpControl saved_;
  const MissionPlayerState* player_{};
  std::array<bool, help_topic_count> inherited_read_{};
};
}  // namespace bw::game
