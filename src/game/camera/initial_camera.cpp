// Shared math retains each caller's scalar/accumulator precision; tolerance
// and failure policy remain here. Both-phase I.9/MEM.9 contracts are in
// bw/math/vector.hpp and matrix.hpp.
// Compose the original snapped behind-camera preset in three collision-limited
// offsets. Header contracts delimit this initial static snapshot from the
// future actor/follow controller. Both MCP phases: R.2 (#rr-use-ptr) makes
// source ownership explicit; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) leaves
// allocation in the injected settings/collision owners. This function uses
// fixed values only. See player-camera-initialization.md for source equations.
#include "initial_camera.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/math/vector.hpp"
namespace bw::game {
namespace {
constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0F;
constexpr float kOriginalAspect = 16.0F / 9.0F;
constexpr float kCameraClearance = 0.05F;
constexpr std::uint32_t kCameraExcludedFlags = 0x2000;
using Vec = math::Vector3<float>;
Vec offset(const Vec& origin, float distance, const Vec& direction) {
  return math::add(origin, math::scale(direction, distance));
}
}  // namespace
InitialPlayerCamera make_initial_player_camera(
    const ScriptedPlayerPose& player,
    const formats::PlayerCameraSettings& settings,
    const scene::StaticCollision& collision) {
  if (!std::isfinite(player.yaw_degrees) ||
      !std::all_of(player.position.begin(), player.position.end(),
                   [](float v) { return std::isfinite(v); })) {
    throw std::invalid_argument("Nonfinite scripted player pose");
  }
  const float yaw = player.yaw_degrees * kDegreesToRadians;
  InitialPlayerCamera result;
  result.pose.right = {std::cos(yaw), 0, -std::sin(yaw)};
  result.pose.up = {0, 1, 0};
  result.pose.forward = {std::sin(yaw), 0, std::cos(yaw)};
  result.perspective = {settings.vertical_fov_radians, kOriginalAspect,
                        settings.near_plane, settings.far_plane};
  // Height establishes the anchor; lateral clearance precedes rear clearance.
  const auto clip = [&](const Vec& from, const Vec& to) {
    return collision
        .clip_segment({from, to, kCameraClearance, kCameraExcludedFlags})
        .position;
  };
  const auto anchor =
      clip(player.position,
           offset(player.position, settings.height, result.pose.up));
  const auto lateral =
      offset(offset(anchor, settings.offset[0], result.pose.right),
             settings.offset[1], result.pose.up);
  const auto shoulder = clip(anchor, lateral);
  result.pose.eye =
      clip(shoulder, offset(shoulder, settings.offset[2], result.pose.forward));
  return result;
}
}  // namespace bw::game
