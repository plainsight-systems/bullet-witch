// Reuse the witnessed source sign/basis reconstruction for every actor.
// Core E.4 validates finite nondegenerate input; MEM.9 keeps fixed copied
// output and zero allocation. Both implementation corpora read.
#include "root_motion.hpp"

#include <cmath>
#include <stdexcept>
namespace bw::game {
animation::Matrix original_root_motion_basis(
    const formats::SkeletonTransform& transform) {
  const auto reference = animation::transform_matrix(transform);
  const math::Vector3<float> authored_up{reference[1][0], reference[1][1],
                                         reference[1][2]};
  auto forward =
      math::Vector3<float>{reference[2][0], reference[2][1], -reference[2][2]};
  auto right = math::cross(authored_up, forward);
  const auto right_length = math::length(right),
             forward_length = math::length(forward);
  if (!std::isfinite(right_length) || !std::isfinite(forward_length) ||
      right_length <= 0 || forward_length <= 0) {
    throw std::runtime_error("Invalid character root reference basis");
  }
  right = math::divide(right, right_length);
  forward = math::divide(forward, forward_length);
  const auto up = math::cross(forward, right);
  auto result = animation::kIdentityMatrix;
  for (std::size_t axis = 0; axis < right.size(); ++axis) {
    result[0].at(axis) = right.at(axis);
    result[1].at(axis) = up.at(axis);
    result[2].at(axis) = forward.at(axis);
  }
  return result;
}
}  // namespace bw::game
