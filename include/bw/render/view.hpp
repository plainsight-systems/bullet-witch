// Convert an explicit rigid camera pose and symmetric perspective frustum to
// portable shader dot-product rows. Game camera ownership, authored presets,
// viewport fitting and material-space transforms remain separate
// responsibilities. Inputs are borrowed only during the call; outputs are
// independent values. All inputs must be finite, the basis orthonormal with
// right cross up = forward (dot-product tolerance 0.0001), 0 < vertical angle <
// pi, aspect > 0 and 0 < near < far. Invalid input or an unrepresentable result
// throws invalid_argument and publishes no transform. Algorithm: validate the
// rigid pose/frustum, invert the basis and translation, then compose the
// original positive-forward-Z, reversed-depth perspective. Each output row dots
// (world_x,world_y,world_z,1); near maps to depth1, far0. Material vectors must
// not use view_rows unless their contract is view-space. Fixed arrays and a
// bounded number of scalar operations require no successful- path allocation or
// synchronization; independent calls are reentrant. See
// research/engine/subsystems/player-camera-initialization.md for original
// source and the deliberate rigid/symmetric scope. Both MCP phases use
// cpp-guidelines I.1 (#ri-explicit) for coordinate/precondition contracts and
// cpp-performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// for fixed storage.
#pragma once

#include <array>

namespace bw::render {
struct ViewPose {
  std::array<float, 3> eye{};
  std::array<float, 3> right{};
  std::array<float, 3> up{};
  std::array<float, 3> forward{};
};
struct PerspectiveView {
  float vertical_fov_radians{};
  float aspect{};
  float near_plane{};
  float far_plane{};
};
struct ViewTransforms {
  std::array<std::array<float, 4>, 3> view_rows{};
  std::array<std::array<float, 4>, 4> clip_rows{};
  // World-coordinate input only: subtract eye without rotating material axes.
  // A model-space caller must compose its model transform separately.
  std::array<std::array<float, 4>, 3> camera_relative_world_rows{};
};

// Build once when the camera changes; draw submissions may share these values.
ViewTransforms make_view_transforms(const ViewPose& pose,
                                    const PerspectiveView& perspective);
}  // namespace bw::render
