// Decode authored initial player-camera settings from the shared customization
// tree. This selects the recovered ordinary initial preset (index two), not a
// guessed eye position. Inputs are borrowed for the call; output owns fixed
// values. Missing, short or nonfinite settings throw resources::Error. Read
// general near/far and behind-camera height/local offset/FOV; validate usable
// frustum values. O(path lookup) work and no additional allocation. Animation,
// zoom changes and follow smoothing remain game-camera responsibilities.
// Both MCP phases: R.2 (#rr-use-ptr) bounds source views; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) retains
// injected tree storage rather than global allocations. Source mapping:
// research/engine/subsystems/player-camera-initialization.md.
#pragma once
#include "bw/formats/customization.hpp"

namespace bw::formats {
struct PlayerCameraSettings {
  float height{};
  std::array<float, 3> offset{};
  std::array<float, 2> pitch_coefficients{};
  float vertical_fov_radians{};
  float near_plane{}, far_plane{};
  float transition_speed{};
  static PlayerCameraSettings parse_initial(
      const CustomizationDefinition& tree);
};
}  // namespace bw::formats
