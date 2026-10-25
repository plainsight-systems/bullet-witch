// Game-owned player/ring command arbitration. Physical shoulder/face edges
// become the original page, expiry and choice priorities here, never in a
// platform backend. Retain one ring lifecycle; observe acquired capabilities
// and live affordability without mutation. An admitted frame routes ring first,
// consumes its face/RS commands, and leaves move/look/fire intact. Paused
// frames do not step the ring's immediate phases. A request is typed and cannot
// claim successful casting without its world owner. Fixed state, no frame
// allocation. Design Core I.1 (#ri-explicit), C.9 (#rc-private) localize policy
// and expose observations only; performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps bounded retained state.
#pragma once
#include "mission_control.hpp"
#include "player_resources.hpp"
#include "spell_selection.hpp"

namespace bw::game {
enum class SpellRequestStatus : std::uint8_t {
  none,
  unavailable,
  missing_world_owner
};
struct PlayerInputResult {
  GameplayInput actions;
  SpellRequestStatus spell_status{};
  std::optional<PlayerSpell> requested_spell;
};
class PlayerInputRouting {
 public:
  PlayerInputResult route(const world::WorldFrame& frame, GameplayInput input,
                          const MissionPlayerState& mission,
                          const PlayerResources& resources);
  SpellRingPresentation ring() const { return selection_.presentation(); }

 private:
  SpellSelectionFlow selection_;
};
}  // namespace bw::game
