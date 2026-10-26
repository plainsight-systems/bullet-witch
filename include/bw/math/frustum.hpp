// Source perspective-volume geometry, independent of rendering and lighting.
// A rigid row-basis camera and left/right/bottom/top/near/far extents produce
// four outward side planes and one far plane; the original sphere admission
// deliberately has no near plane. Boundary contact is admitted. Inputs must
// be finite, camera axes orthonormal/right-handed, and extents nondegenerate
// with 0 < near < far; invalid inputs/results throw invalid_argument.
// Fixed owned arrays have no lifetime dependency, allocation or
// synchronization. Algorithm: transform four near-plane corner directions,
// cross their source edge pairs, normalize the planes, offset by eye, then
// append the far plane. Queries test signed plane distance against sphere
// radius in source order. Source: lighting-scene-producer-contract.md, original
// 40a860/40b2a0. Both phases: I.1 (#ri-explicit) exposes axes and near-plane
// omission; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// retains fixed storage.
#pragma once

#include <stdexcept>

#include "matrix.hpp"

namespace bw::math {
inline constexpr std::size_t kPerspectivePlaneCount = 5;
inline constexpr std::size_t kPerspectiveExtentCount = 6;
struct PerspectiveFrustum {
  std::array<Vector<float, 4>, kPerspectivePlaneCount> planes{};
  float depth_range{};
};

inline PerspectiveFrustum make_perspective_frustum(
    const Affine3<float>& camera_world,
    const std::array<float, kPerspectiveExtentCount>& extents) {
  constexpr std::size_t kFar = 5;
  constexpr float kRigidTolerance = .0001F;
  for (const auto& row : camera_world) {
    if (!all_finite(row)) {
      throw std::invalid_argument("Nonfinite frustum camera");
    }
  }
  if (!all_finite(extents) || !(extents[0] < extents[1]) ||
      !(extents[2] < extents[3]) || !(extents[4] > 0) ||
      !(extents[kFar] > extents[4])) {
    throw std::invalid_argument("Invalid perspective frustum extents");
  }
  for (std::size_t row = 0; row < 3; ++row) {
    if (std::abs(dot(camera_world.at(row), camera_world.at(row)) - 1.F) >
        kRigidTolerance) {
      throw std::invalid_argument("Nonrigid frustum camera");
    }
    for (std::size_t other = row + 1; other < 3; ++other) {
      if (std::abs(dot(camera_world.at(row), camera_world.at(other))) >
          kRigidTolerance) {
        throw std::invalid_argument("Nonorthogonal frustum camera");
      }
    }
  }
  if (dot(cross(camera_world[0], camera_world[1]), camera_world[2]) <= 0) {
    throw std::invalid_argument("Reversed frustum camera basis");
  }
  const std::array<Vector3<float>, 4> local{
      {{extents[0], extents[3], extents[4]},
       {extents[1], extents[3], extents[4]},
       {extents[1], extents[2], extents[4]},
       {extents[0], extents[2], extents[4]}}};
  std::array<Vector3<float>, 4> corners{};
  for (std::size_t i = 0; i < corners.size(); ++i) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      corners.at(i).at(axis) = local.at(i)[0] * camera_world[0].at(axis) +
                               local.at(i)[1] * camera_world[1].at(axis) +
                               local.at(i)[2] * camera_world[2].at(axis);
    }
  }
  constexpr std::array<std::array<std::size_t, 2>, 4> kEdges{
      {{3, 0}, {1, 2}, {2, 3}, {0, 1}}};
  PerspectiveFrustum result;
  result.depth_range = extents[kFar] - extents[4];
  for (std::size_t i = 0; i < kEdges.size(); ++i) {
    const auto perpendicular =
        cross(corners.at(kEdges.at(i)[0]), corners.at(kEdges.at(i)[1]));
    const float magnitude = static_cast<float>(
        std::sqrt(static_cast<double>(dot(perpendicular, perpendicular))));
    const auto normal = scale(perpendicular, 1.F / magnitude);
    result.planes.at(i) = {normal[0], normal[1], normal[2],
                           -dot(camera_world[3], normal)};
  }
  const auto far_point =
      add(scale(camera_world[2], extents[kFar]), camera_world[3]);
  result.planes.back() = {camera_world[2][0], camera_world[2][1],
                          camera_world[2][2], -dot(camera_world[2], far_point)};
  for (const auto& plane : result.planes) {
    if (!all_finite(plane)) {
      throw std::invalid_argument("Unrepresentable frustum");
    }
  }
  return result;
}

// The supplied planes must be finite/unit-normal, as produced by the factory.
// No normalization or near-plane policy is added during sphere admission.
inline bool intersects(const PerspectiveFrustum& frustum,
                       const Vector3<float>& center, float radius) {
  for (const auto& plane : frustum.planes) {
    const float distance = plane[0] * center[0] + plane[1] * center[1] +
                           plane[2] * center[2] + plane[3];
    if (radius < distance) return false;
  }
  return true;
}
}  // namespace bw::math
