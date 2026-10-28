// Resolve a copied game action into one prepared playback request. The actor
// chooses its catalog, slot mapping and event-parameter ownership; this helper
// owns only validation and original seconds/frames/phase conversion. Catalog
// and current playback are borrowed for the call, and the result retains only
// indices and values. Missing slots return nullopt; invalid timing rejects
// before any bank mutates. Fixed work/storage, no allocation or clock advance.
// Core I.11 (#ri-raw), read for design and implementation, separates prepared
// data from live ownership; CDSA.32 consumes the shared prepared catalog.
#pragma once
#include "action_motion_plan.hpp"
#include "character_motion_player.hpp"

namespace bw::game {
std::optional<CharacterMotionRequest> resolve_action_motion(
    const CharacterAnimations& catalog, std::optional<std::size_t> clip,
    const ActionMotionRequest& request,
    const animation::ClipPlaybackState* current, bool motion_parameters);
}  // namespace bw::game
