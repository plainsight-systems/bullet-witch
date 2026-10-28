// Matrix-to-quaternion branches adapted from Jolt Physics, Copyright (c) 2021
// Jorrit Rouwe, MIT license: docs/licenses/JoltPhysics-LICENSE.
// Extract a quaternion from an orthonormal row-vector rotation. Validate finite
// right-handed basis against a caller-selected tolerance, then use the stable
// trace/largest-diagonal branches also used by pinned Jolt
// Mat44::GetQuaternion. Return nullopt for scale, shear, reflection or
// nonfinite input; normalize only roundoff in the resulting quaternion.
// Translation/homogeneous lanes are not consumed. Fixed arithmetic/storage with
// no state, allocation or ownership. Both phases Core I.11 (#ri-raw) returns
// owned values; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retains fixed storage.
// Orthogonal affine decomposition additionally separates axis scale using the
// pinned Unreal TTransform::SetFromMatrix/ExtractScaling contract. It returns
// scale explicitly (reflection on X), never mutates the input and rejects
// singular/non-affine/sheared bases. Both phases I.4 (#ri-typed) returns named
// components; MEM.9 keeps decomposition fixed-storage.
// Algorithm reference and convention checks: first-combat-physics-boundary.md.
#pragma once
#include "matrix.hpp"
namespace bw::math {
template <std::floating_point T>
std::optional<Vector<T, 4>> rigid_rotation(const Matrix4<T>& matrix,
                                           T tolerance) {
  if (!std::isfinite(tolerance) || tolerance < 0) {
    return std::nullopt;
  }
  const auto basis = affine_part(matrix);
  for (std::size_t row = 0; row < 3; ++row) {
    for (const auto value : basis.at(row)) {
      if (!std::isfinite(value)) {
        return std::nullopt;
      }
    }
    if (std::abs(dot(basis.at(row), basis.at(row)) - 1) > tolerance) {
      return std::nullopt;
    }
    for (std::size_t other = 0; other < row; ++other) {
      if (std::abs(dot(basis.at(row), basis.at(other))) > tolerance) {
        return std::nullopt;
      }
    }
  }
  if (std::abs(dot(cross(basis[0], basis[1]), basis[2]) - 1) > tolerance) {
    return std::nullopt;
  }
  Vector<T, 4> quaternion{};
  const T trace = matrix[0][0] + matrix[1][1] + matrix[2][2];
  constexpr T half = T{0.5};
  if (trace >= 0) {
    const T root = std::sqrt(trace + 1), factor = half / root;
    quaternion = {(matrix[1][2] - matrix[2][1]) * factor,
                  (matrix[2][0] - matrix[0][2]) * factor,
                  (matrix[0][1] - matrix[1][0]) * factor, half * root};
  } else {
    std::size_t largest = 0;
    if (matrix[1][1] > matrix[0][0]) {
      largest = 1;
    }
    if (matrix[2][2] > matrix.at(largest).at(largest)) {
      largest = 2;
    }
    const std::size_t next = (largest + 1) % 3, last = (next + 1) % 3;
    const T root =
        std::sqrt(matrix.at(largest).at(largest) - matrix.at(next).at(next) -
                  matrix.at(last).at(last) + 1);
    const T factor = half / root;
    quaternion.at(largest) = half * root;
    quaternion.at(next) =
        (matrix.at(largest).at(next) + matrix.at(next).at(largest)) * factor;
    quaternion.at(last) =
        (matrix.at(largest).at(last) + matrix.at(last).at(largest)) * factor;
    quaternion[3] =
        (matrix.at(next).at(last) - matrix.at(last).at(next)) * factor;
  }
  return divide(quaternion, std::sqrt(accumulated_dot(quaternion, quaternion)));
}
// Separate scale before rigid extraction. The caller owns whether that scale
// belongs to geometry, diagnostics, or another explicitly documented policy.
template <std::floating_point T>
std::optional<TransformComponents<T>> decompose_orthogonal_affine(
    const Matrix4<T>& matrix, T tolerance) {
  if (!all_finite(matrix) || matrix[0][3] != 0 || matrix[1][3] != 0 ||
      matrix[2][3] != 0 || matrix[3][3] != 1) {
    return std::nullopt;
  }
  auto rigid = matrix;
  Vector3<T> scale{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const Vector3<T> row{matrix.at(axis)[0], matrix.at(axis)[1],
                         matrix.at(axis)[2]};
    scale.at(axis) = length(row);
    if (!std::isfinite(scale.at(axis)) || scale.at(axis) <= 0) {
      return std::nullopt;
    }
    for (std::size_t component = 0; component < 3; ++component) {
      rigid.at(axis).at(component) /= scale.at(axis);
    }
  }
  const auto basis = affine_part(rigid);
  if (dot(cross(basis[0], basis[1]), basis[2]) < 0) {
    scale[0] = -scale[0];
    for (std::size_t component = 0; component < 3; ++component) {
      rigid[0].at(component) = -rigid[0].at(component);
    }
  }
  const auto rotation = rigid_rotation(rigid, tolerance);
  if (!rotation) return std::nullopt;
  return TransformComponents<T>{*rotation, translation(matrix), scale};
}
}  // namespace bw::math
