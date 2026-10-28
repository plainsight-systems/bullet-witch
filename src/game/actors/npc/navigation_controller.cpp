// Source5ce940 owns distance hysteresis; route finding and ground projection
// remain separate capabilities. Validate a whole request before changing state,
// retain the chosen phase across calls, and return steering without moving an
// actor. I.1 (#ri-explicit) exposes source settings/clock inputs; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps this controller
// allocation-free. Both implementation corpora were read.
#include "navigation_controller.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bw::game {
namespace {
constexpr float kThresholdBias = 0.25F;
constexpr float kDirectionMinimumSquared = 1.0000001111620804e-6F;
void validate(const NpcNavigationRequest& request,
              const NpcNavigationFrame& frame) {
  for (const float value :
       {request.approach_start_squared, request.approach_stop_squared,
        request.retreat_start_squared, request.retreat_distance,
        request.minimum_squared, frame.delta_seconds,
        frame.idle_threshold_adjustment, frame.retreat_arrival_squared}) {
    if (!std::isfinite(value)) {
      throw std::invalid_argument("Nonfinite NPC navigation input");
    }
  }
  if (!math::all_finite(request.destination) ||
      !math::all_finite(frame.position) || frame.delta_seconds < 0 ||
      request.minimum_squared < 0 || frame.retreat_arrival_squared < 0) {
    throw std::invalid_argument("Invalid NPC navigation input");
  }
}
}  // namespace
NpcNavigationController::NpcNavigationController(
    const NpcNavigationQueries& queries, NpcRouteFollower& route)
    : queries_(queries), route_(route) {}
void NpcNavigationController::reset() {
  state_ = State::idle;
  heading_ = {};
  route_.reset();
}
NpcNavigationResult NpcNavigationController::advance(
    const NpcNavigationRequest& request, const NpcNavigationFrame& frame) {
  validate(request, frame);
  NpcNavigationResult result{.destination = request.destination};
  if (frame.delta_seconds == 0) {
    return result;
  }
  float start = std::max(request.approach_start_squared + kThresholdBias,
                         request.minimum_squared);
  float stop = std::max(request.approach_stop_squared + kThresholdBias,
                        request.minimum_squared);
  if (!route_.has_route()) {
    start += frame.idle_threshold_adjustment;
    stop += frame.idle_threshold_adjustment;
  }
  const auto relative = math::subtract(frame.position, request.destination);
  const float distance_squared = math::dot(relative, relative);
  // Source transitions do not execute the new state's movement until the next
  // admitted update. This prevents entry from consuming an extra motion tick.
  if (state_ == State::idle) {
    const bool visible = queries_.visible(request.destination);
    if (distance_squared < request.retreat_start_squared && visible) {
      state_ = State::retreat;
    } else if (start <= distance_squared || !visible) {
      state_ = State::approach;
    }
    result.arrived = state_ == State::idle;
    return result;
  }
  result.retreating = state_ == State::retreat;
  auto destination = request.destination;
  float arrival = stop;
  if (state_ == State::approach) {
    destination = queries_.project(frame.position, destination, false);
  } else {
    const auto away = NpcNavigationVector{relative[0], 0, relative[2]};
    if (math::dot(away, away) < kDirectionMinimumSquared) {
      state_ = State::idle;
      result.destination = frame.position;
      result.arrived = true;
      return result;
    }
    destination =
        math::add(destination,
                  math::scale(math::normalize_or(away, NpcNavigationVector{}),
                              request.retreat_distance));
    destination = queries_.project(frame.position, destination, true);
    arrival = frame.retreat_arrival_squared;
  }
  const auto route = route_.advance(frame.position, destination, arrival,
                                    frame.delta_seconds, request.trace_path);
  result.destination = route.waypoint;
  result.blocked = route.blocked;
  auto direction = math::subtract(route.waypoint, frame.position);
  direction[1] = 0;
  result.direction = math::normalize_or(direction, NpcNavigationVector{});
  result.moving = !route.arrived && !route.blocked;
  if (result.moving) {
    heading_ = result.direction;
  }
  const float horizontal_squared =
      relative[0] * relative[0] + relative[2] * relative[2];
  const bool reached =
      state_ == State::approach
          ? horizontal_squared < stop && queries_.visible(request.destination)
          : request.retreat_distance * request.retreat_distance <=
                distance_squared;
  if (reached) {
    state_ = State::idle;
    result.destination = math::add(
        frame.position, math::scale(heading_, request.minimum_squared));
    result.moving = false;
    result.arrived = true;
  }
  return result;
}
}  // namespace bw::game
