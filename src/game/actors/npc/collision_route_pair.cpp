// Consume source paired-worker states without conflating per-step readiness
// with completed route exploration. Actor calls only schedule work; world
// calls run pending steps. All geometry is borrowed per call. Both phases:
// I.1 (#ri-explicit) names these capabilities; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retains two fixed
// jobs.
#include "collision_route_pair.hpp"

#include <cmath>
#include <stdexcept>
namespace bw::game {
namespace {
constexpr float kLookAhead = .1F, kGroundRange = 2, kMaximumStep = .3F;
constexpr float kNearRetainedSquared = .1F, kRefreshRangeScale = 16;
constexpr std::int32_t kStepBudget = 512;
}  // namespace
CollisionRouteRequest CollisionRoutePair::job_request(
    const CollisionRouteVector& position,
    const CollisionRouteVector& target) const {
  return {position, target, config_};
}
CollisionRoutePairSample CollisionRoutePair::sample() const {
  const auto first = jobs_[0].sample();
  return {published_, phase_ != Phase::initial, phase_ == Phase::following,
          (phase_ == Phase::waiting || phase_ == Phase::following) &&
              first.boundary};
}
void CollisionRoutePair::cancel() {
  for (auto& job : jobs_) job.cancel();
  phase_ = Phase::initial;
}
void CollisionRoutePair::advance_jobs(const CollisionRouteQueries& queries) {
  for (auto& job : jobs_) job.advance(queries);
}
CollisionRoutePairSample CollisionRoutePair::advance_actor(
    const CollisionRoutePairRequest& request,
    const CollisionRoutePairQueries& queries) {
  if (!math::all_finite(request.position) ||
      !math::all_finite(request.destination) ||
      !std::isfinite(request.body_radius) || request.body_radius < kLookAhead ||
      (request.side != -1 && request.side != 1)) {
    throw std::invalid_argument("Invalid collision route pair request");
  }
  switch (phase_) {
    case Phase::initial: {
      constexpr float kStepScale = 1.0099999904632568F;
      body_radius_ = request.body_radius;
      const float step = body_radius_ * kStepScale;
      const float arrival = step * 1.5F;
      config_ = {
          arrival * arrival,
          step,
          kGroundRange,
          kMaximumStep,
          body_radius_,
          std::sqrt(body_radius_ * body_radius_ - kLookAhead * kLookAhead),
          kStepBudget,
          request.side};
      retained_ = published_ = queries.initial_waypoint(
          request.position, request.destination, body_radius_);
      for (auto& job : jobs_) {
        job.request(job_request(request.position, request.destination));
      }
      phase_ = Phase::waiting;
      break;
    }
    case Phase::refresh:
      for (auto& job : jobs_) {
        job.request(job_request(request.position, request.destination));
      }
      secondary_handoff_ = true;
      phase_ = Phase::waiting;
      break;
    case Phase::waiting:
      if (jobs_[0].sample().ready && jobs_[1].sample().ready) {
        retained_ = published_ = jobs_[0].sample().current;
        if (request.delayed) {
          remaining_ = 4;
          phase_ = Phase::delay;
        } else {
          phase_ = Phase::following;
          for (auto& job : jobs_) job.requeue();
          remaining_ = config_.step_budget;
        }
      }
      break;
    case Phase::delay:
      --remaining_;
      if (remaining_ < 0) {
        remaining_ = config_.step_budget;
        phase_ = Phase::following;
      }
      break;
    case Phase::following:
      follow(request, queries);
      break;
  }
  return sample();
}
// Follow the first result while the second continues exploratory work. Preserve
// the source's publication hold near a boundary and secondary-best handoff
// order.
void CollisionRoutePair::follow(const CollisionRoutePairRequest& request,
                                const CollisionRoutePairQueries& queries) {
  const auto first = jobs_[0].sample();
  if (first.ready) {
    --remaining_;
    if (remaining_ < 0) phase_ = Phase::refresh;
    auto from = request.position;
    from[1] -= body_radius_;
    auto retained_offset = math::subtract(retained_, request.position);
    retained_offset[1] = 0;
    if (queries.clear(from, first.output, config_.step, config_.maximum_step) ||
        math::dot(retained_offset, retained_offset) < kNearRetainedSquared) {
      retained_ = first.output;
      if (math::squared_distance(retained_, request.position) >=
              config_.arrival_squared * 4 ||
          !((phase_ == Phase::waiting || phase_ == Phase::following) &&
            first.boundary)) {
        published_ = retained_;
      }
      jobs_[0].requeue();
    } else {
      published_ = retained_;
    }
  }
  const auto second = jobs_[1].sample();
  if (second.ready && !second.complete) jobs_[1].requeue();
  if (second.complete && secondary_handoff_) {
    jobs_[0].request(job_request(request.position, second.best));
    secondary_handoff_ = false;
  }
  if (jobs_[0].sample().complete &&
      math::squared_distance(retained_, request.position) <
          config_.arrival_squared * kRefreshRangeScale) {
    // Both restart toward the latest destination, not the exploratory best.
    jobs_[1].request(job_request(request.position, request.destination));
    jobs_[0].request(job_request(request.position, request.destination));
    secondary_handoff_ = true;
    published_ = retained_;
  }
}
}  // namespace bw::game
