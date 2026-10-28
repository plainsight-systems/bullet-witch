// Original actor-owned pair of collision route jobs. Actor advance consumes
// copied positions and completed job results; world advance alone processes
// pending jobs. Both jobs retain the same chosen side, independent progress and
// readiness distinct from search completion. Initial waypoint, delayed
// admission, periodic refresh and the secondary best-point handoff follow the
// recovered pair algorithm documented in first-combat-ai.md. Queries borrow
// immutable geometry synchronously. No actor/world/clock pointer is retained.
// One owner thread calls both phases serially; cancel releases pending work.
// Fixed two-job storage allocates nothing. Core I.1 (#ri-explicit) exposes
// phase capabilities; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds storage.
#pragma once
#include "collision_route_search.hpp"
namespace bw::game {
struct CollisionRoutePairRequest {
  CollisionRouteVector position{}, destination{};
  float body_radius{};
  // The caller selects source automatic/forced side before first admission.
  // This owner retains it through refresh; only cancel admits a new side.
  std::int32_t side{1};
  bool delayed{};
};
class CollisionRoutePairQueries : public CollisionRouteQueries {
 public:
  virtual CollisionRouteVector initial_waypoint(
      const CollisionRouteVector& position,
      const CollisionRouteVector& destination, float body_radius) const = 0;
};
struct CollisionRoutePairSample {
  CollisionRouteVector waypoint{};
  // Pair following admission persists while individual jobs are requeued;
  // it is not the worker's one-step ready flag or search-complete flag.
  bool active{}, request_pair_ready{}, searching_boundary{};
};
class CollisionRoutePair {
 public:
  CollisionRoutePair() = default;
  ~CollisionRoutePair() = default;
  CollisionRoutePair(const CollisionRoutePair&) = delete;
  CollisionRoutePair& operator=(const CollisionRoutePair&) = delete;
  CollisionRoutePair(CollisionRoutePair&&) = delete;
  CollisionRoutePair& operator=(CollisionRoutePair&&) = delete;
  CollisionRoutePairSample advance_actor(const CollisionRoutePairRequest&,
                                         const CollisionRoutePairQueries&);
  void advance_jobs(const CollisionRouteQueries&);
  void cancel();
  CollisionRoutePairSample sample() const;

 private:
  enum class Phase : std::uint8_t {
    initial,
    refresh,
    waiting,
    delay,
    following
  };
  CollisionRouteRequest job_request(const CollisionRouteVector& position,
                                    const CollisionRouteVector& target) const;
  void follow(const CollisionRoutePairRequest&,
              const CollisionRoutePairQueries&);
  std::array<CollisionRouteSearch, 2> jobs_;
  CollisionRouteConfig config_{};
  CollisionRouteVector retained_{}, published_{};
  float body_radius_{};
  std::int32_t remaining_{};
  Phase phase_{Phase::initial};
  // Defined-state adaptation: inspected constructors do not initialize this
  // native byte. Subsequent source set/clear points and reset retention match.
  bool secondary_handoff_{};
};
}  // namespace bw::game
