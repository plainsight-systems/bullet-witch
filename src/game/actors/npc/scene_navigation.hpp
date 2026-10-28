// Per-actor route progress and cover requests over immutable mission geometry.
// The scene owns decoded paths/cover points and the query implementation; these
// nonmoving owners borrow them and never mutate actors, physics or clocks.
// Route construction allocates scratch for the admitted graph. Advance checks
// arrival/direct visibility, searches original links when available, and
// publishes the next waypoint. Without an archive, the original outer route
// owner admits a retained two-job collision search when direct clearance fails.
// Actor calls consume copied worker results; world calls alone advance jobs.
// Direct and routed movement admission remain separate source policies.
// Shared physical contact resolves actual actor clearance.
// Original graph traversal remains opaque: A* relaxation/open heap and segment
// progress follow pinned Unreal GraphAStar/PathFollowingComponent, documented
// as inference. Cover scoring follows recovered native predicates, retaining
// request/result independently of action phases. All updates use admitted time.
// Core I.30 (#ri-encapsulate) isolates native layouts; MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) bounds scratch before
// actor admission.
// Route-pair revision: I.1 (#ri-explicit) and MEM.9 were reread for both
// interface and implementation; query capabilities are const synchronous
// borrows, random state is explicit, and two fixed jobs allocate nothing.
#pragma once
#include "bw/content/formats/navigation_archive.hpp"
#include "collision_route_pair.hpp"
#include "game/session/compatibility/source_random.hpp"
#include "navigation_controller.hpp"

namespace bw::game {
struct NpcCoverPoint {
  NpcNavigationVector position{}, direction{};
};
struct NpcCoverObstacle {
  NpcNavigationVector position{};
  float radius{};
};
class NpcRouteQueries {
 public:
  NpcRouteQueries() = default;
  virtual ~NpcRouteQueries() = default;
  NpcRouteQueries(const NpcRouteQueries&) = delete;
  NpcRouteQueries& operator=(const NpcRouteQueries&) = delete;
  NpcRouteQueries(NpcRouteQueries&&) = delete;
  NpcRouteQueries& operator=(NpcRouteQueries&&) = delete;
  // Source body-clearance sensor, not just a center ray. Read-only shared
  // collision provider performs the sweep with the actor's copied dimensions.
  virtual bool traversable(const NpcNavigationVector& from,
                           const NpcNavigationVector& to) const = 0;
  virtual bool occluded(const NpcNavigationVector& from,
                        const NpcNavigationVector& to) const = 0;
  virtual std::optional<NpcNavigationVector> ground(
      const NpcNavigationVector& point) const = 0;
  virtual std::span<const NpcCoverPoint> cover_points() const = 0;
  virtual std::span<const NpcCoverObstacle> cover_obstacles() const = 0;
};
class NpcCollisionRouteQueries : public CollisionRoutePairQueries {
 public:
  virtual std::optional<NpcNavigationVector> route_face(
      const NpcNavigationVector& from, const NpcNavigationVector& to) const = 0;
};
struct NpcRouteContext {
  NpcRouteContext(SourceRandom& stream, const NpcCollisionRouteQueries& queries,
                  float radius, bool enabled)
      : random(stream),
        collision(queries),
        body_radius(radius),
        collision_search_enabled(enabled) {}
  std::reference_wrapper<SourceRandom> random;
  std::reference_wrapper<const NpcCollisionRouteQueries> collision;
  float body_radius{};
  bool collision_search_enabled{};
};
class NpcSceneRouteFollower final : public NpcRouteFollower {
 public:
  NpcSceneRouteFollower(const formats::NavigationArchive&, std::size_t group,
                        const NpcRouteQueries&, NpcRouteContext,
                        const std::shared_ptr<memory::Domain>&);
  ~NpcSceneRouteFollower() override = default;
  NpcSceneRouteFollower(const NpcSceneRouteFollower&) = delete;
  NpcSceneRouteFollower& operator=(const NpcSceneRouteFollower&) = delete;
  NpcSceneRouteFollower(NpcSceneRouteFollower&&) = delete;
  NpcSceneRouteFollower& operator=(NpcSceneRouteFollower&&) = delete;
  NpcRouteResult advance(const NpcNavigationVector& position,
                         const NpcNavigationVector& destination,
                         float arrival_squared, float delta_seconds,
                         bool trace_path) override;
  void reset() override;
  bool has_route() const override { return active_; }
  void set_side(std::int32_t side);
  void observe_sensor(bool permitted) noexcept {
    sensor_permitted_ = permitted;
  }
  void advance_jobs() { pair_.advance_jobs(context_.collision); }
  bool collision_route_active() const noexcept { return collision_active_; }
  bool pair_ready() const noexcept { return pair_.sample().request_pair_ready; }

 private:
  struct OpenNode {
    std::size_t index{};
    float score{};
  };
  struct Endpoints {
    NpcNavigationVector position{}, destination{};
  };
  bool search(const Endpoints&);
  NpcRouteResult collision_route(const Endpoints&, float seconds);
  std::int32_t choose_side(const Endpoints&);
  const formats::NavigationArchive& archive_;
  const NpcRouteQueries& queries_;
  NpcRouteContext context_;
  CollisionRoutePair pair_;
  std::size_t group_{};
  memory::Vector<float> costs_;
  memory::Vector<std::size_t> parents_, route_;
  memory::Vector<std::uint8_t> closed_;
  memory::Vector<OpenNode> open_;
  NpcNavigationVector destination_{};
  std::size_t cursor_{};
  bool active_{}, blocked_{};
  std::int32_t side_{};
  std::int32_t chosen_side_{1};
  float sensor_failure_delay_{};
  bool sensor_permitted_{true}, retained_sensor_permitted_{};
  bool collision_active_{};
};
struct NpcCoverRequest {
  NpcNavigationVector position{}, threat{};
  float body_radius{}, body_height{};
  std::int32_t mode{};
};
struct NpcCoverResult {
  NpcNavigationVector position{}, direction{};
  bool ready{}, found{};
};
class NpcCoverSearch {
 public:
  explicit NpcCoverSearch(const NpcRouteQueries& queries)
      : queries_(&queries) {}
  ~NpcCoverSearch() = default;
  NpcCoverSearch(const NpcCoverSearch&) = delete;
  NpcCoverSearch& operator=(const NpcCoverSearch&) = delete;
  NpcCoverSearch(NpcCoverSearch&&) noexcept = default;
  NpcCoverSearch& operator=(NpcCoverSearch&&) noexcept = default;
  void request(const NpcCoverRequest&);
  void advance(float delta_seconds);
  void reset();
  NpcCoverResult result() const { return result_; }

 private:
  const NpcRouteQueries* queries_{};
  NpcCoverRequest request_{};
  NpcCoverResult result_{};
  bool pending_{};
};
}  // namespace bw::game
