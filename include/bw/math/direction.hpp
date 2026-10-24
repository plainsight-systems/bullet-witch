// Fixed-vector angular stepping with explicit near-collinear axis policy.
// Inputs are finite unit directions, maximum_angle is in [0, pi], and the
// finite unit fallback axis is not parallel to current. Policy requires
// collinear_cosine in [0,1) and minimum_axis_squared >= 0. Callers validate
// these preconditions; this arithmetic layer does not repair invalid inputs.
// Snap exactly to target within the angular step. Otherwise derive the turn
// axis from current cross target inside the inclusive alignment interval;
// outside it, use the caller's fallback. Construct normalized right/forward
// and advance sin(step)*right + cos(step)*forward. Near antipodes the fallback
// deliberately chooses a turn side, which need not be the shortest arc.
// Fixed work/storage, no allocation or mutable state. See the source witness
// in player-direction-reference.md. Both design/implementation: Core I.1
// (#ri-explicit), I.7 (#ri-post) state axis/threshold/result contracts;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keeps the simulation operation allocation-free.
#pragma once
#include "bw/math/vector.hpp"

namespace bw::math {
template <std::floating_point T>
struct DirectionTurnPolicy {
  Vector3<T> fallback_axis;
  T collinear_cosine;
  T minimum_axis_squared;
};
template <std::floating_point T>
Vector3<T> turn_toward(const Vector3<T>& current, const Vector3<T>& target,
                       T maximum_angle, const DirectionTurnPolicy<T>& policy) {
  const T alignment = dot(current, target);
  const T cosine = std::cos(maximum_angle);
  if (alignment >= cosine) return target;
  // Collinear directions cannot choose a stable turn side from their cross.
  auto axis = policy.fallback_axis;
  if (alignment >= -policy.collinear_cosine &&
      alignment <= policy.collinear_cosine) {
    const auto candidate = cross(current, target);
    const T squared = dot(candidate, candidate);
    if (squared > 0 && squared >= policy.minimum_axis_squared) {
      axis = scale(candidate, T{1} / std::sqrt(squared));
    }
  }
  // Preserve the source basis construction: right first, then forward.
  const auto right = cross(axis, current);
  const auto unit_right = scale(right, T{1} / length(right));
  const auto unit_forward = scale(current, T{1} / length(current));
  return add(scale(unit_right, std::sin(maximum_angle)),
             scale(unit_forward, cosine));
}
}  // namespace bw::math
