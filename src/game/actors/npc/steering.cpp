// Evaluate source cohesion/alignment/separation/forward avoidance from retained
// world requests, then publish copied results. E.4 rejects invalid admission
// before mutation; CDSA.9 uses fixed contiguous scans at this bounded scale.
// Both implementation corpora retrieved. Normalization and orientation math
// use shared utilities; this service cannot move an actor or step physics.
#include "steering.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/math/direction.hpp"
#include "bw/math/matrix.hpp"
#include "bw/math/quaternion.hpp"
namespace bw::game {
namespace {
using Vector = math::Vector3<float>;
constexpr float kMinimumSquared = 1.0000001111620804e-6F;
Vector unit(const Vector& value) {
  const auto squared = math::dot(value, value);
  return squared >= kMinimumSquared
             ? math::scale(value, 1.F / std::sqrt(squared))
             : Vector{};
}
void validate(const NpcSteeringRequest& request) {
  constexpr float kUnitTolerance = .001F;
  const auto& s = request.settings;
  if (request.identity == 0 || !math::all_finite(request.position) ||
      !math::all_finite(request.heading) ||
      std::abs(math::dot(request.heading, request.heading) - 1) >
          kUnitTolerance) {
    throw std::invalid_argument("Invalid NPC steering sensor pose");
  }
  for (const auto value :
       {s.cohesion_cosine, s.cohesion_squared, s.separation_cosine,
        s.separation_squared, s.avoidance_threshold, s.aggregate_weight,
        s.cohesion_weight, s.alignment_weight, s.separation_weight,
        s.avoidance_weight}) {
    if (!std::isfinite(value)) {
      throw std::invalid_argument("Nonfinite NPC steering settings");
    }
  }
}
}  // namespace
Vector NpcSteeringSample::direction(bool include_cohesion) const {
  return unit(include_cohesion ? math::add(cohesion, other) : other);
}
NpcSteeringSample NpcSteeringSensors::refresh(
    const NpcSteeringRequest& request) {
  validate(request);
  auto found = std::ranges::find_if(entries_, [&](const Entry& entry) {
    return entry.request.identity == request.identity;
  });
  if (found == entries_.end()) {
    found = std::ranges::find_if(entries_, [](const Entry& entry) {
      return entry.request.identity == 0;
    });
    if (found == entries_.end()) {
      throw std::length_error("NPC sensor capacity exceeded");
    }
  }
  if (!found->pending) {
    found->request = request;
    found->pending = true;
  }
  return found->sample;
}
void NpcSteeringSensors::retire(std::uint64_t identity) noexcept {
  const auto found = std::ranges::find_if(entries_, [&](const Entry& entry) {
    return entry.request.identity == identity;
  });
  if (found != entries_.end()) *found = {};
}
void NpcSteeringSensors::advance(const scene::StaticCollision& collision) {
  for (auto& entry : entries_) {
    if (!entry.pending) continue;
    const auto& self = entry.request;
    const auto& settings = self.settings;
    Vector centroid{}, alignment{}, separation{}, avoidance{};
    std::size_t count = 0;
    for (const auto& neighbor : entries_) {
      const auto& other = neighbor.request;
      if (other.identity == 0 || other.identity == self.identity) continue;
      const auto offset = math::subtract(other.position, self.position);
      const auto squared = math::dot(offset, offset);
      if (squared < kMinimumSquared) continue;
      const auto direction = math::scale(offset, 1.F / std::sqrt(squared));
      const auto cosine = math::dot(self.heading, direction);
      const bool gather = cosine > settings.cohesion_cosine &&
                          squared < settings.cohesion_squared;
      const bool separate = cosine > settings.separation_cosine &&
                            squared < settings.separation_squared;
      if (!gather && !separate) continue;
      // Visibility admits centroid/alignment/separation; the source projected
      // forward term is evaluated independently after classification.
      constexpr float kVisibilityClearance = .01F;
      if (!collision
               .clip_segment(
                   {self.position, other.position, kVisibilityClearance, 0})
               .clipped) {
        if (gather) {
          ++count;
          centroid = math::add(centroid, other.position);
          alignment = math::add(alignment, other.heading);
        }
        if (separate) {
          separation = math::add(
              separation,
              math::scale(direction, squared - settings.separation_squared));
        }
      }
      const auto along = math::dot(self.heading, offset);
      if (along <= 0) continue;
      const auto perpendicular =
          math::subtract(math::scale(self.heading, along), offset);
      const auto lateral_squared = math::dot(perpendicular, perpendicular);
      const auto denominator = lateral_squared + .01F;
      if (lateral_squared >= kMinimumSquared &&
          denominator < settings.avoidance_threshold) {
        avoidance = math::add(
            avoidance, math::scale(unit(perpendicular),
                                   (settings.avoidance_threshold - squared) /
                                       denominator));
      }
    }
    entry.sample = {};
    if (count > 0) {
      entry.sample.cohesion = math::scale(
          unit(math::subtract(math::divide(centroid, static_cast<float>(count)),
                              self.position)),
          settings.cohesion_weight);
      entry.sample.other =
          math::scale(unit(alignment), settings.alignment_weight);
    }
    entry.sample.other = math::add(
        entry.sample.other,
        math::add(math::scale(unit(separation), settings.separation_weight),
                  math::scale(unit(avoidance), settings.avoidance_weight)));
    entry.pending = false;
  }
}
void NpcSteeringHeading::reset(const Vector& heading) {
  if (!math::all_finite(heading) ||
      math::dot(heading, heading) < kMinimumSquared) {
    throw std::invalid_argument("Invalid NPC route heading");
  }
  heading_ = unit(heading);
  suppression_ = 0;
}
Vector NpcSteeringHeading::advance(const Vector& goal_direction,
                                   const NpcSteeringSample& neighbors,
                                   float weight, bool approach, float seconds) {
  if (!math::all_finite(goal_direction) || !std::isfinite(weight) ||
      !std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid NPC local steering frame");
  }
  if (seconds == 0) return heading_;
  auto goal = goal_direction;
  constexpr float kGoalMinimumSquared = .0001F;
  if (math::dot(goal, goal) >= kGoalMinimumSquared) goal = unit(goal);
  auto desired = math::add(
      goal,
      math::scale(neighbors.direction(approach && suppression_ <= 0), weight));
  desired[1] = 0;
  if (math::dot(desired, desired) >= kMinimumSquared) {
    desired = unit(desired);
    const auto maximum = std::min(seconds * 2 * std::numbers::pi_v<float>,
                                  std::numbers::pi_v<float>);
    const auto capped =
        math::turn_toward(heading_, desired, maximum,
                          math::DirectionTurnPolicy<float>{
                              {0, 1, 0}, .9990000128746033F, kMinimumSquared});
    // Both source directions are horizontal: yaw quaternions represent their
    // orthonormal frames without introducing an actor transform dependency.
    const auto first = math::quaternion_axis_angle(
        Vector{0, 1, 0}, std::atan2(heading_[0], heading_[2]));
    const auto second = math::quaternion_axis_angle(
        Vector{0, 1, 0}, std::atan2(capped[0], capped[2]));
    const auto blended = math::quaternion_slerp(
        first, second, math::SphericalBlend<float>{.4F, 1.F});
    const auto basis = math::affine_transform(
        math::TransformComponents<float>{blended, {}, {1, 1, 1}});
    heading_ = {basis[2][0], basis[2][1], basis[2][2]};
  }
  suppression_ -= seconds;
  return heading_;
}
}  // namespace bw::game
