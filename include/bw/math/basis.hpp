// Orthonormal row basis with an explicit collinearity/fallback policy. Finite,
// nonzero forward and finite up/policy axes are caller preconditions; selected
// fallback right must not be parallel to forward. Cross up with forward;
// exactly collinear inputs choose one of two caller-supplied alternate ups by
// forward Y. Below the caller's right-length threshold use its default right.
// Normalize forward and the final cross-derived up independently. Fixed value
// work/storage, no allocation, clock or state. Source409b70 supplies one
// policy; this helper contains no game constants. Both phases Core I.1
// (#ri-explicit), I.5 (#ri-pre) expose policy and singularity preconditions;
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// bounds scratch.
#pragma once
#include "matrix.hpp"

namespace bw::math {
template <std::floating_point T>
struct BasisFallback {
  T forward_y_split{}, minimum_right_length{};
  Vector3<T> lower_up{}, upper_up{}, default_right{};
};
template <std::floating_point T>
Matrix4<T> orthonormal_basis_with_fallback(const Vector3<T>& forward,
                                           const Vector3<T>& up,
                                           const BasisFallback<T>& policy) {
  auto right = cross(up, forward);
  if (dot(right, right) <= 0) {
    right = cross(forward[1] <= policy.forward_y_split ? policy.lower_up
                                                       : policy.upper_up,
                  forward);
  }
  const T right_length = length(right);
  right = right_length >= policy.minimum_right_length
              ? divide(right, right_length)
              : policy.default_right;
  const auto unit_forward = divide(forward, length(forward));
  const auto derived_up = cross(forward, right);
  const auto unit_up = divide(derived_up, length(derived_up));
  return {{{right[0], right[1], right[2], 0},
           {unit_up[0], unit_up[1], unit_up[2], 0},
           {unit_forward[0], unit_forward[1], unit_forward[2], 0},
           {0, 0, 0, 1}}};
}
}  // namespace bw::math
