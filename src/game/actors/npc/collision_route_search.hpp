// One original collision-driven route job and its retained boundary walk.
// The pair/actor owner submits or requeues; the world alone advances one
// pending step per admitted frame. Readiness means a step was processed, not
// arrival. Queries borrow geometry only during advance and return copied
// observations; this job cannot move actors, advance clocks, or retain
// collision providers. Request validates before replacement. Cancel discards
// pending/results. Ready jobs do no work until explicitly requeued. Fixed state
// has no heap allocation; each boundary step tries at most four directions.
// Source evidence and pair ownership: first-combat-ai.md (collision route
// worker). Core I.1
// (#ri-explicit) separates submit/read/advance capabilities; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds hot-path
// storage.
#pragma once
#include <array>
#include <cstdint>

#include "bw/math/vector.hpp"
namespace bw::game {
using CollisionRouteVector = math::Vector3<float>;
struct CollisionRouteConfig {
  float arrival_squared{}, step{}, ground_up{}, maximum_step{};
  float clearance_radius{}, lateral_radius{};
  std::int32_t step_budget{}, side{1};
};
struct CollisionRouteRequest {
  CollisionRouteVector position{}, destination{};
  CollisionRouteConfig config{};
};
struct CollisionRouteGroundRequest {
  CollisionRouteVector point{};
  float ground_up{}, maximum_step{};
};
struct CollisionRouteGround {
  CollisionRouteVector point{};
  bool valid{};
};
class CollisionRouteQueries {
 public:
  CollisionRouteQueries() = default;
  virtual ~CollisionRouteQueries() = default;
  CollisionRouteQueries(const CollisionRouteQueries&) = delete;
  CollisionRouteQueries& operator=(const CollisionRouteQueries&) = delete;
  CollisionRouteQueries(CollisionRouteQueries&&) = delete;
  CollisionRouteQueries& operator=(CollisionRouteQueries&&) = delete;
  virtual CollisionRouteGround ground(
      const CollisionRouteGroundRequest&) const = 0;
  virtual bool support(const CollisionRouteVector&,
                       const CollisionRouteVector& direction, float ground_up,
                       float maximum_step, float radius,
                       float lateral_radius) const = 0;
  virtual bool clear(const CollisionRouteVector& from,
                     const CollisionRouteVector& to, float radius,
                     float height) const = 0;
};
struct CollisionRouteSample {
  CollisionRouteVector output{}, current{}, best{};
  bool ready{}, complete{}, boundary{}, pending{};
};
class CollisionRouteSearch {
 public:
  CollisionRouteSearch() = default;
  ~CollisionRouteSearch() = default;
  CollisionRouteSearch(const CollisionRouteSearch&) = delete;
  CollisionRouteSearch& operator=(const CollisionRouteSearch&) = delete;
  CollisionRouteSearch(CollisionRouteSearch&&) = delete;
  CollisionRouteSearch& operator=(CollisionRouteSearch&&) = delete;
  void request(const CollisionRouteRequest&);
  void requeue();
  void advance(const CollisionRouteQueries&);
  void cancel();
  CollisionRouteSample sample() const;

 private:
  enum class Phase : std::uint8_t { initialize, straight, boundary, complete };
  enum class Status : std::uint8_t { idle, pending, ready };
  CollisionRouteVector direction(unsigned index) const;
  bool admissible(const CollisionRouteQueries&, CollisionRouteVector&,
                  const CollisionRouteVector&) const;
  bool arrived(const CollisionRouteQueries&, const CollisionRouteVector&) const;
  void legal_start(const CollisionRouteQueries&) const;
  void begin_boundary();
  bool advance_boundary(const CollisionRouteQueries&);
  CollisionRouteRequest request_{};
  CollisionRouteVector heading_{}, current_{}, output_{};
  CollisionRouteVector boundary_start_{}, boundary_current_{}, best_{};
  float best_squared_{};
  std::array<std::uint8_t, 4> directions_{};
  std::int32_t initial_direction_{-1}, remaining_{};
  Phase phase_{Phase::initialize};
  Status status_{Status::idle};
};
}  // namespace bw::game
