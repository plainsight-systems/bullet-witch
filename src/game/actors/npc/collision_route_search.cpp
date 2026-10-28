// Retain original straight/boundary worker phases and step publication order.
// Source queries remain synchronous and immutable; ready is distinct from done.
// Both implementation corpora: Core I.1 (#ri-explicit) exposes scheduling and
// query inputs; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// retains fixed storage and bounded four-direction probes without allocation.
#include "collision_route_search.hpp"

#include <cmath>
#include <stdexcept>

namespace bw::game {
void CollisionRouteSearch::request(const CollisionRouteRequest& value) {
  const auto& c = value.config;
  if (!math::all_finite(value.position) ||
      !math::all_finite(value.destination) ||
      !std::isfinite(c.arrival_squared) || c.arrival_squared < 0 ||
      !std::isfinite(c.step) || c.step <= 0 || !std::isfinite(c.ground_up) ||
      c.ground_up < 0 || !std::isfinite(c.maximum_step) || c.maximum_step < 0 ||
      !std::isfinite(c.clearance_radius) || c.clearance_radius < 0 ||
      !std::isfinite(c.lateral_radius) || c.lateral_radius < 0 ||
      c.step_budget < 0 || (c.side != 1 && c.side != -1)) {
    throw std::invalid_argument("Invalid collision route request");
  }
  request_ = value;
  auto heading = math::subtract(value.destination, value.position);
  heading[1] = 0;
  constexpr float kMinimumSquared = 1.0000001111620804e-6F;
  const auto squared = math::dot(heading, heading);
  heading_ = squared >= kMinimumSquared
                 ? math::divide(heading, std::sqrt(squared))
                 : CollisionRouteVector{0, 0, 1};
  current_ = output_ = best_ = value.position;
  remaining_ = c.step_budget;
  initial_direction_ = -1;
  phase_ = Phase::initialize;
  status_ = Status::pending;
}
void CollisionRouteSearch::requeue() {
  if (status_ == Status::ready) status_ = Status::pending;
}
void CollisionRouteSearch::cancel() {
  status_ = Status::idle;
  phase_ = Phase::initialize;
  current_ = output_ = best_ = {};
}
CollisionRouteSample CollisionRouteSearch::sample() const {
  return {output_,
          current_,
          best_,
          status_ == Status::ready,
          status_ != Status::idle && phase_ == Phase::complete,
          status_ != Status::idle && phase_ == Phase::boundary,
          status_ == Status::pending};
}
CollisionRouteVector CollisionRouteSearch::direction(unsigned index) const {
  switch (index) {
    case 0:
      return {-heading_[2], heading_[1], heading_[0]};
    case 1:
      return heading_;
    case 2:
      return {heading_[2], heading_[1], -heading_[0]};
    case 3:
      return {-heading_[0], heading_[1], -heading_[2]};
    default:
      throw std::logic_error("Invalid collision route direction");
  }
}
bool CollisionRouteSearch::admissible(
    const CollisionRouteQueries& queries, CollisionRouteVector& candidate,
    const CollisionRouteVector& heading) const {
  const auto& c = request_.config;
  const auto ground = queries.ground({candidate, c.ground_up, c.maximum_step});
  candidate = ground.point;
  return ground.valid &&
         queries.support(candidate, heading, c.ground_up, c.maximum_step,
                         c.step, c.lateral_radius);
}
bool CollisionRouteSearch::arrived(const CollisionRouteQueries& queries,
                                   const CollisionRouteVector& point) const {
  const auto& c = request_.config;
  return math::squared_distance(point, request_.destination) <
             c.arrival_squared &&
         queries.clear(request_.destination, point, c.step, c.maximum_step);
}
// Original initialization probes a local copy. Its adjustment is deliberately
// not published into the worker's current point.
void CollisionRouteSearch::legal_start(
    const CollisionRouteQueries& queries) const {
  const auto& c = request_.config;
  auto candidate = current_;
  if (admissible(queries, candidate, heading_)) return;
  constexpr std::array<unsigned, 4> kOrder{1, 3, 2, 0};
  for (const auto index : kOrder) {
    const auto heading = direction(index);
    candidate = math::add(current_, math::scale(heading, c.step));
    if (admissible(queries, candidate, heading) &&
        queries.clear(current_, candidate, c.clearance_radius,
                      c.maximum_step)) {
      static_cast<void>(
          queries.ground({candidate, c.ground_up, c.maximum_step}));
      return;
    }
  }
  constexpr float kStartRetreatScale = -1.2F;
  candidate =
      math::add(current_, math::scale(heading_, kStartRetreatScale * c.step));
  static_cast<void>(queries.ground({candidate, c.ground_up, c.maximum_step}));
}
void CollisionRouteSearch::begin_boundary() {
  boundary_start_ = boundary_current_ = best_ = current_;
  best_squared_ = math::squared_distance(current_, request_.destination);
  initial_direction_ = -1;
  directions_ = {};
}
bool CollisionRouteSearch::advance_boundary(
    const CollisionRouteQueries& queries) {
  const auto& c = request_.config;
  if (initial_direction_ < 0) {
    constexpr std::array<unsigned, 4> kPositive{0, 3, 2, 1};
    constexpr std::array<unsigned, 4> kNegative{2, 3, 0, 1};
    const auto& order = c.side == 1 ? kPositive : kNegative;
    initial_direction_ = static_cast<std::int32_t>(order.front());
    for (const auto index : order) {
      const auto heading = direction(index);
      auto candidate = math::add(current_, math::scale(heading, c.step));
      if (admissible(queries, candidate, heading)) {
        initial_direction_ = static_cast<std::int32_t>(index);
        break;
      }
    }
    boundary_start_ = boundary_current_ = current_;
    directions_.fill(static_cast<std::uint8_t>(initial_direction_));
  }
  auto next = static_cast<std::uint8_t>(directions_.front());
  auto index = (static_cast<int>(next) + c.side + 4) % 4;
  for (unsigned attempt = 0; attempt < 4; ++attempt) {
    const auto heading = direction(static_cast<unsigned>(index));
    auto candidate = math::add(boundary_current_, math::scale(heading, c.step));
    if (admissible(queries, candidate, heading)) {
      boundary_current_ = candidate;
      const auto distance =
          math::squared_distance(candidate, request_.destination);
      if (distance < best_squared_) {
        best_squared_ = distance;
        best_ = candidate;
      }
      next = static_cast<std::uint8_t>((index + 2) % 4);
      break;
    }
    index = (index + c.side + 4) % 4;
  }
  for (std::size_t i = directions_.size() - 1; i > 0; --i) {
    directions_.at(i) = directions_.at(i - 1);
  }
  directions_.front() = next;
  bool circulated = true;
  for (std::size_t i = 1; i < directions_.size(); ++i) {
    const auto expected =
        (static_cast<int>(next) + static_cast<int>(i) * c.side + 4) % 4;
    circulated = circulated && directions_.at(i) == expected;
  }
  return circulated ||
         (next == initial_direction_ &&
          math::squared_distance(boundary_current_, boundary_start_) <
              c.step * c.step);
}
// One world admission consumes exactly one pending worker update. Boundary
// mode publishes the preceding point before advancing the wall follower.
void CollisionRouteSearch::advance(const CollisionRouteQueries& queries) {
  if (status_ != Status::pending) return;
  if (phase_ == Phase::complete) {
    status_ = Status::ready;
    return;
  }
  // Only the negative predicate is observable; retain it without overflow.
  if (remaining_ >= 0) --remaining_;
  const auto& c = request_.config;
  switch (phase_) {
    case Phase::initialize:
      legal_start(queries);
      phase_ = Phase::straight;
      remaining_ = c.step_budget;
      break;
    case Phase::straight: {
      auto candidate = math::add(current_, math::scale(heading_, c.step));
      // The source executes all three queries, even after failed grounding.
      const auto ground =
          queries.ground({candidate, c.ground_up, c.maximum_step});
      candidate = ground.point;
      const bool supported =
          queries.support(candidate, heading_, c.ground_up, c.maximum_step,
                          c.step, c.lateral_radius);
      const bool clear = queries.clear(current_, candidate, c.clearance_radius,
                                       c.maximum_step);
      if (!(ground.valid && supported && clear)) {
        phase_ = Phase::boundary;
        begin_boundary();
        static_cast<void>(advance_boundary(queries));
      } else {
        current_ = output_ = candidate;
        if (arrived(queries, current_)) {
          begin_boundary();
          phase_ = Phase::complete;
        }
      }
      break;
    }
    case Phase::boundary:
      if (remaining_ >= 0) --remaining_;
      output_ = boundary_current_;
      if (advance_boundary(queries) || remaining_ < 0 ||
          arrived(queries, boundary_current_)) {
        phase_ = Phase::complete;
      }
      break;
    case Phase::complete:
      break;
  }
  status_ = Status::ready;
}
}  // namespace bw::game
