// Shared row-vector CPU transforms. Matrix<T,R,C> stores rows; affine matrices
// have four rows/three columns, with translation in row 3. Homogeneous matrices
// have four rows/columns. Points include translation; directions do not.
// Functions borrow fixed arrays and return owned fixed values without
// allocation, synchronization or mutation. Multiplication accumulates from zero
// in ascending inner order. Affine point conversion explicitly names
// accumulator precision and starts with translation, preserving the
// placed-geometry contract. Quaternion construction retains authored magnitude
// instead of normalizing it. Singular/nonfinite determinant returns no inverse;
// other arithmetic propagates IEEE values for caller-specific validation.
// Work/storage are bounded by static dimensions. No target dispatch or
// reciprocal approximations. See shared-math.md. Both phases: Core I.9
// (#ri-concepts) constrains scalar/accumulator types, I.1
// (#ri-explicit) names row/point conventions; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps operations
// fixed.
#pragma once
#include <optional>

#include "vector.hpp"

namespace bw::math {
template <std::floating_point T, std::size_t Rows, std::size_t Columns>
using Matrix = std::array<Vector<T, Columns>, Rows>;
template <std::floating_point T>
using Affine3 = Matrix<T, 4, 3>;
template <std::floating_point T>
using Matrix4 = Matrix<T, 4, 4>;

// Shape conversion preserves the affine lanes exactly. Extracting the affine
// part discards the homogeneous column; callers requiring an affine transform
// must validate that column (the homogeneous inverse overload does so).
template <std::floating_point T>
constexpr Affine3<T> affine_part(const Matrix4<T>& matrix) {
  Affine3<T> result{};
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      result.at(row).at(column) = matrix.at(row).at(column);
    }
  }
  return result;
}
template <std::floating_point T>
constexpr Matrix4<T> homogeneous(const Affine3<T>& matrix) {
  Matrix4<T> result{};
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      result.at(row).at(column) = matrix.at(row).at(column);
    }
  }
  result[3][3] = 1;
  return result;
}
template <std::floating_point T, std::size_t Columns>
  requires(Columns >= 3)
constexpr Vector3<T> translation(const Matrix<T, 4, Columns>& matrix) {
  return {matrix[3][0], matrix[3][1], matrix[3][2]};
}
// Construct an orthonormal row basis from a nonzero forward and nonparallel
// up vector. Normalize right first, then forward, and derive up by cross.
// Callers own singular/fallback policy; IEEE results propagate if violated.
template <std::floating_point T>
Matrix4<T> orthonormal_basis(const Vector3<T>& forward, const Vector3<T>& up) {
  const auto cross_right = cross(up, forward);
  const auto right = divide(cross_right, length(cross_right));
  const auto unit_forward = divide(forward, length(forward));
  const auto unit_up = cross(unit_forward, right);
  return {{{right[0], right[1], right[2], 0},
           {unit_up[0], unit_up[1], unit_up[2], 0},
           {unit_forward[0], unit_forward[1], unit_forward[2], 0},
           {0, 0, 0, 1}}};
}
// Change only translation, retaining the basis and homogeneous lanes.
template <std::floating_point T, std::size_t Columns>
  requires(Columns >= 3)
constexpr void set_translation(Matrix<T, 4, Columns>& matrix,
                               const Vector3<T>& value) {
  for (std::size_t column = 0; column < 3; ++column) {
    matrix[3].at(column) = value.at(column);
  }
}

template <std::floating_point T, std::size_t Rows, std::size_t Inner,
          std::size_t Columns>
constexpr Matrix<T, Rows, Columns> multiply(
    const Matrix<T, Rows, Inner>& left,
    const Matrix<T, Inner, Columns>& right) {
  Matrix<T, Rows, Columns> result{};
  for (std::size_t row = 0; row < Rows; ++row) {
    for (std::size_t column = 0; column < Columns; ++column) {
      for (std::size_t inner = 0; inner < Inner; ++inner) {
        result.at(row).at(column) +=
            left.at(row).at(inner) * right.at(inner).at(column);
      }
    }
  }
  return result;
}
// Exchange axes without changing scalar values or allocating storage.
template <std::floating_point T, std::size_t Rows, std::size_t Columns>
constexpr Matrix<T, Columns, Rows> transpose(
    const Matrix<T, Rows, Columns>& input) {
  Matrix<T, Columns, Rows> result{};
  for (std::size_t row = 0; row < Rows; ++row) {
    for (std::size_t column = 0; column < Columns; ++column) {
      result.at(column).at(row) = input.at(row).at(column);
    }
  }
  return result;
}
template <std::floating_point T>
struct TransformComponents {
  Vector<T, 4> rotation;
  Vector3<T> translation, scale;
};
// Build the original non-unit-quaternion rotation and scale each basis row.
template <std::floating_point T>
constexpr Affine3<T> affine_transform(const TransformComponents<T>& transform) {
  const auto [x, y, z, w] = transform.rotation;
  Affine3<T> result{{{w * w + x * x - y * y - z * z, 2 * (x * y + w * z),
                      2 * (x * z - w * y)},
                     {2 * (x * y - w * z), w * w - x * x + y * y - z * z,
                      2 * (w * x + y * z)},
                     {2 * (w * y + x * z), 2 * (y * z - w * x),
                      w * w - x * x - y * y + z * z},
                     transform.translation}};
  for (std::size_t row = 0; row < 3; ++row) {
    for (auto& value : result.at(row)) value *= transform.scale.at(row);
  }
  return result;
}
template <std::floating_point T>
constexpr Matrix4<T> homogeneous_transform(
    const TransformComponents<T>& transform) {
  const auto affine = affine_transform(transform);
  Matrix4<T> result{};
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      result.at(row).at(column) = affine.at(row).at(column);
    }
  }
  // Preserve the old scaled zero lanes, including negative scale signed zero.
  for (std::size_t row = 0; row < 3; ++row) {
    result.at(row)[3] = T{0} * transform.scale.at(row);
  }
  result[3][3] = 1;
  return result;
}
// Invert the affine basis first, then transform and negate its translation.
template <std::floating_point T>
std::optional<Affine3<T>> inverse_affine(const Affine3<T>& matrix) {
  const auto [a, b, c] = matrix[0];
  const auto [d, e, f] = matrix[1];
  const auto [g, h, i] = matrix[2];
  const T determinant =
      a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (!std::isfinite(determinant) || determinant == 0) return std::nullopt;
  Affine3<T> result{{{e * i - f * h, c * h - b * i, b * f - c * e},
                     {f * g - d * i, a * i - c * g, c * d - a * f},
                     {d * h - e * g, b * g - a * h, a * e - b * d},
                     {0, 0, 0}}};
  for (std::size_t row = 0; row < 3; ++row) {
    for (auto& value : result.at(row)) value /= determinant;
  }
  for (std::size_t column = 0; column < 3; ++column) {
    for (std::size_t row = 0; row < 3; ++row) {
      result[3].at(column) -= matrix[3].at(row) * result.at(row).at(column);
    }
  }
  return result;
}
// Homogeneous storage uses the same affine inverse algorithm. Reject a
// perspective/noncanonical last column rather than silently discarding it.
template <std::floating_point T>
std::optional<Matrix4<T>> inverse_affine(const Matrix4<T>& matrix) {
  if (matrix[0][3] != 0 || matrix[1][3] != 0 || matrix[2][3] != 0 ||
      matrix[3][3] != 1) {
    return std::nullopt;
  }
  const auto inverse = inverse_affine(affine_part(matrix));
  if (!inverse) return std::nullopt;
  return homogeneous(*inverse);
}
template <std::floating_point Accumulator, std::floating_point T,
          std::size_t Columns>
  requires(Columns >= 3)
Vector3<T> transform_point(const Vector3<T>& point,
                           const Matrix<T, 4, Columns>& matrix) {
  Vector3<T> result{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    Accumulator value = matrix[3].at(axis);
    for (std::size_t row = 0; row < 3; ++row) {
      value +=
          static_cast<Accumulator>(point.at(row)) * matrix.at(row).at(axis);
    }
    result.at(axis) = static_cast<T>(value);
  }
  return result;
}
// Accumulate the three basis products before translation. This source variant
// preserves boundary comparisons where translation-first rounding differs.
// Fixed values only; CP.3 immutable inputs, MEM.9 no allocation (both phases).
template <std::floating_point T, std::size_t Columns>
  requires(Columns >= 3)
Vector3<T> transform_point_basis_first(const Vector3<T>& point,
                                       const Matrix<T, 4, Columns>& matrix) {
  Vector3<T> result{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    result.at(axis) = point[0] * matrix[0].at(axis) +
                      point[1] * matrix[1].at(axis) +
                      point[2] * matrix[2].at(axis) + matrix[3].at(axis);
  }
  return result;
}
// Directions use only the basis, preserving explicit accumulation precision.
template <std::floating_point Accumulator, std::floating_point T,
          std::size_t Columns>
  requires(Columns >= 3)
Vector3<T> transform_direction(const Vector3<T>& direction,
                               const Matrix<T, 4, Columns>& matrix) {
  Vector3<T> result{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    Accumulator value = 0;
    for (std::size_t row = 0; row < 3; ++row) {
      value +=
          static_cast<Accumulator>(direction.at(row)) * matrix.at(row).at(axis);
    }
    result.at(axis) = static_cast<T>(value);
  }
  return result;
}
template <std::floating_point T, std::size_t Rows, std::size_t Columns>
bool all_finite(const Matrix<T, Rows, Columns>& matrix) {
  for (const auto& row : matrix) {
    if (!all_finite(row)) return false;
  }
  return true;
}
}  // namespace bw::math
