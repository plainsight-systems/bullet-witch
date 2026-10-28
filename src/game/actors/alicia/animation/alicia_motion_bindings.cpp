// Preserve the accepted player policy through the shared original slot
// publisher. NPCs use its common view; Alicia explicitly requests PW overrides.
// I.11/CDSA.32 lifetime and layout contracts are in source_motion_bindings.hpp.
#include "alicia_motion_bindings.hpp"

#include "game/actors/animation/source_motion_bindings.hpp"
namespace bw::game {
std::span<const animation::ClipAliasRequest> alicia_motion_aliases() {
  return player_motion_aliases();
}
}  // namespace bw::game
