// Sole user-authorized temporary initial-placement shim for the animation
// review slice. Accept only the exact One Shot Kill mission/stage pair; return
// its fresh-start pose by value, or throw invalid_argument. No checkpoint,
// script execution, mission readiness or general spawn policy is implemented.
// Input strings borrow only for the call; no state, allocation or threading
// dependency. Remove this module when genuine mission startup admits the
// original SetPlayerPosition/SetPlayerDirection commands. Provenance and the
// explicit exception are in
// research/engine/subsystems/mission-start-admission.md. Both MCP phases: I.1
// (#ri-explicit) makes identity/admission explicit; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) keeps
// this fixed value adapter allocation-free, outside resource lifetime
// ownership.
#pragma once

#include <string_view>

#include "game/player/camera/initial_camera.hpp"

namespace bw::game {
ScriptedPlayerPose initial_placement_shim(std::string_view mission,
                                          std::string_view stage,
                                          bool restoring_checkpoint);
}  // namespace bw::game
