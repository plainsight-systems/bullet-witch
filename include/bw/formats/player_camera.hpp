// Decode authored ordinary/targeting player-camera settings from the shared
// customization tree. Retain standing/crouched presets and independent look
// rates; initial compatibility fields select preset two. Inputs
// are borrowed for the call; output owns fixed values. Missing, short,
// nonfinite or invalid ranges throw resources::Error without partial
// publication. Read general near/far and behind-camera offsets/FOV/rate;
// validate usable ranges. O(path lookup) work, no additional allocation.
// Runtime look, follow and zoom remain game-camera responsibilities. Both MCP
// phases: I.1 (ri-explicit) makes rate/limit contracts explicit; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retains fixed values.
// Original tree ownership: R.2 (#rr-use-ptr) and MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md).
// Source: player-camera-initialization.md and player-camera-follow.md.
#pragma once
#include "bw/formats/customization.hpp"

namespace bw::formats {
// Four source presets retain camera context independently from raised
// animation. Ordered ordinary standing, targeting standing, ordinary crouched,
// targeting crouched; each carries its own authored horizontal/vertical look
// rates.
struct PlayerCameraPreset {
  float height{};
  std::array<float, 3> offset{};
  std::array<float, 2> pitch_coefficients{};
  float vertical_fov_radians{};
  std::array<float, 2> look_radians_per_second{};
};
struct PlayerCameraSettings {
  float height{};
  std::array<float, 3> offset{};
  std::array<float, 2> pitch_coefficients{};
  float vertical_fov_radians{};
  float near_plane{}, far_plane{};
  float transition_speed{};
  float look_radians_per_second{}, pitch_limit_radians{};
  std::array<PlayerCameraPreset, 4> contexts{};
  bool automatic_targeting_camera{};
  static PlayerCameraSettings parse_initial(
      const CustomizationDefinition& tree);
};
}  // namespace bw::formats
