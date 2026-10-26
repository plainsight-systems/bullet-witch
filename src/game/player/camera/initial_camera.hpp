// Compose the recovered initial behind-player camera from an explicit scripted
// actor pose, authored settings and registered static collision. The caller
// owns mission/actor initialization; this function does not execute or replace
// Lua. Inputs are borrowed for the call, output is independent. Validate finite
// pose, construct the original yaw basis, snap to the initial authored preset,
// then clip vertical, lateral and rear offsets in that order using original
// clearance. The result preserves player yaw and the original fixed-aspect
// frustum. Static collision cost dominates; no allocations or mutable
// persistent state. Moving actors, follow interpolation and camera input are
// outside this bounded initialization contract. Both MCP phases: R.2
// (#rr-use-ptr) bounds borrowed inputs; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) keeps
// allocations in the injected collision/tree owners. Source evidence:
// research/engine/subsystems/player-camera-initialization.md.
#pragma once
#include "bw/render/scene/view.hpp"
#include "bw/spatial/static_collision.hpp"
#include "player_camera_settings.hpp"

namespace bw::game {
struct ScriptedPlayerPose {
  std::array<float, 3> position{};
  float yaw_degrees{};
};
struct InitialPlayerCamera {
  render::ViewPose pose;
  render::PerspectiveView perspective;
};
InitialPlayerCamera make_initial_player_camera(
    const ScriptedPlayerPose& player,
    const formats::PlayerCameraSettings& settings,
    const scene::StaticCollision& collision);
}  // namespace bw::game
