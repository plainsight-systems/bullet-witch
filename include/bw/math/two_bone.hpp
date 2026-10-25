// Analytic two-link inverse kinematics in one caller-selected coordinate space.
// Inputs are borrowed values; the result owns solved joint/end positions and
// shortest-arc rotation deltas (x,y,z,w) for the two existing segment frames.
// No allocation, global state, skeleton mutation, clock or game policy.
// Unreal AnimationCore SolveTwoBoneIK is the established algorithm reference;
// this implementation uses independent triangle equations, with both inner
// and outer reach clamping. See support-arm-constraint.md for the pinned source
// and the deliberate improvement over the original PC's inactive solver.
// DESIGN/IMPLEMENTATION: Core I.1 (#ri-explicit) names spaces, reach and
// failure contracts; F.20 (#rf-out) returns owned results; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds all local
// storage.
#pragma once
#include <limits>
#include <optional>

#include "bw/math/quaternion.hpp"

namespace bw::math {
template <std::floating_point T>
struct TwoBoneRequest {
  Vector3<T> root, joint, end;
  Vector3<T> target, pole;
  T upper_length, lower_length;
};
template <std::floating_point T>
struct TwoBoneSolution {
  Vector3<T> joint, end;
  Vector<T, 4> upper_rotation, lower_rotation;
  bool reach_clamped;
};
// Preserve positive segment lengths without stretching. Targets outside
// [abs(upper-lower), upper+lower] are projected onto that reachable interval.
// The pole chooses the elbow half-plane; a collinear pole retains the current
// elbow plane, then uses a deterministic perpendicular for a straight chain.
// A coincident target retains the current root-to-end direction (or upper
// segment direction). Coincident equal-length endpoints fold in the bend plane.
// Nonfinite inputs, nonpositive lengths, zero current segments, or arithmetic
// overflow return nullopt. No partial update is exposed on failure.
template <std::floating_point T>
std::optional<TwoBoneSolution<T>> solve_two_bone(
    const TwoBoneRequest<T>& request);

namespace detail {
template <std::floating_point T>
Vector3<T> two_bone_perpendicular(const Vector3<T>& direction) {
  std::size_t lane = 0;
  for (std::size_t i = 1; i < direction.size(); ++i) {
    if (std::abs(direction.at(i)) < std::abs(direction.at(lane))) lane = i;
  }
  Vector3<T> basis{};
  basis.at(lane) = 1;
  const auto projected =
      subtract(basis, scale(direction, dot(basis, direction)));
  return divide(projected, length(projected));
}
template <std::floating_point T>
struct TwoBoneDirectionChange {
  Vector3<T> from, to;
};
template <std::floating_point T>
Vector<T, 4> two_bone_rotation(const TwoBoneDirectionChange<T>& change) {
  const auto a = divide(change.from, length(change.from));
  const auto b = divide(change.to, length(change.to));
  const auto rotation =
      rotation_between(DirectionRotation<T>{a, b, two_bone_perpendicular(a)});
  return scale(rotation, T{1} / std::sqrt(accumulated_dot(rotation, rotation)));
}
}  // namespace detail

template <std::floating_point T>
std::optional<TwoBoneSolution<T>> solve_two_bone(
    const TwoBoneRequest<T>& request) {
  const auto& [root, joint, end, target, pole, upper, lower] = request;
  if (!all_finite(root) || !all_finite(joint) || !all_finite(end) ||
      !all_finite(target) || !all_finite(pole) || !std::isfinite(upper) ||
      !std::isfinite(lower) || upper <= 0 || lower <= 0) {
    return std::nullopt;
  }
  const auto old_upper = subtract(joint, root);
  const auto old_lower = subtract(end, joint);
  const T old_upper_length = length(old_upper);
  const T old_lower_length = length(old_lower);
  const T outer_radius = upper + lower;
  const T inner_radius = std::abs(upper - lower);
  const auto desired = subtract(target, root);
  const T requested_distance = length(desired);
  if (!std::isfinite(old_upper_length) || !std::isfinite(old_lower_length) ||
      old_upper_length <= 0 || old_lower_length <= 0 ||
      !std::isfinite(outer_radius) || !std::isfinite(requested_distance)) {
    return std::nullopt;
  }
  // Preserve an existing direction for a coincident goal; do not invent a
  // model/world axis when the input limb already defines one.
  auto direction = divide(old_upper, old_upper_length);
  const auto old_reach = subtract(end, root);
  const T old_reach_length = length(old_reach);
  if (!std::isfinite(old_reach_length)) return std::nullopt;
  if (old_reach_length > 0) direction = divide(old_reach, old_reach_length);
  if (requested_distance > 0) direction = divide(desired, requested_distance);

  // The projected pole fixes the bend plane. At a degenerate pole retain the
  // existing elbow side, with a deterministic orthogonal axis as the last case.
  const auto pole_delta = subtract(pole, root);
  auto bend =
      subtract(pole_delta, scale(direction, dot(pole_delta, direction)));
  constexpr T angular_epsilon = std::numeric_limits<T>::epsilon() * T{64};
  const T pole_length = length(pole_delta);
  T bend_length = length(bend);
  if (!std::isfinite(pole_length) || !std::isfinite(bend_length)) {
    return std::nullopt;
  }
  if (bend_length <= angular_epsilon * pole_length) {
    bend = subtract(old_upper, scale(direction, dot(old_upper, direction)));
    bend_length = length(bend);
    if (bend_length <= angular_epsilon * old_upper_length) {
      bend = detail::two_bone_perpendicular(direction);
      bend_length = 1;
    }
  }
  bend = divide(bend, bend_length);

  // Solve the triangle directly. Clamping its reachable radius also handles
  // goals closer than the difference of the two segment lengths.
  const T distance = std::clamp(requested_distance, inner_radius, outer_radius);
  const T projection =
      distance >= outer_radius ? upper
      : distance <= inner_radius && inner_radius > 0
          ? (upper > lower ? upper : -upper)
      : distance > 0 ? (upper * upper - lower * lower + distance * distance) /
                           (T{2} * distance)
                     : T{0};
  if (!std::isfinite(projection)) return std::nullopt;
  const T along = std::clamp(projection, -upper, upper);
  const T height_squared = upper * upper - along * along;
  if (!std::isfinite(height_squared)) return std::nullopt;
  const T height = std::sqrt(std::max(T{0}, height_squared));
  const auto solved_joint =
      add(root, add(scale(direction, along), scale(bend, height)));
  const auto solved_end = add(root, scale(direction, distance));
  if (!all_finite(solved_joint) || !all_finite(solved_end)) return std::nullopt;
  const auto upper_rotation =
      detail::two_bone_rotation(detail::TwoBoneDirectionChange<T>{
          .from = old_upper, .to = subtract(solved_joint, root)});
  const auto lower_rotation =
      detail::two_bone_rotation(detail::TwoBoneDirectionChange<T>{
          .from = old_lower, .to = subtract(solved_end, solved_joint)});
  if (!all_finite(upper_rotation) || !all_finite(lower_rotation)) {
    return std::nullopt;
  }
  return TwoBoneSolution<T>{solved_joint, solved_end, upper_rotation,
                            lower_rotation, distance != requested_distance};
}
}  // namespace bw::math
