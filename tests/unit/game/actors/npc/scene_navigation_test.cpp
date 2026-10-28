// Public route/cover ownership tests: missing graphs select local steering;
// physical contact owns clearance, and cover results retain source
// visibility and distance gates. Core SL.con.3 and MEM.9 cover bounded
// immutable fixtures.
#include "game/actors/npc/scene_navigation.hpp"

#include <cmath>
#include <numbers>

#include "../../../../fixtures/navigation_fixture.hpp"
#include "game/actors/npc/steering.hpp"
namespace {
using bw::game::NpcNavigationVector;
using camera_fixture::require;
class Queries final : public bw::game::NpcRouteQueries,
                      public bw::game::NpcCollisionRouteQueries {
 public:
  bool traversable(const NpcNavigationVector& from,
                   const NpcNavigationVector& to) const override {
    const auto delta = bw::math::subtract(to, from);
    return pass && (!graph_segments || bw::math::dot(delta, delta) <= 1.01F);
  }
  bool occluded(const NpcNavigationVector&,
                const NpcNavigationVector&) const override {
    return hidden;
  }
  std::optional<NpcNavigationVector> ground(
      const NpcNavigationVector& p) const override {
    return p;
  }
  std::span<const bw::game::NpcCoverPoint> cover_points() const override {
    return points;
  }
  std::span<const bw::game::NpcCoverObstacle> cover_obstacles() const override {
    return {};
  }
  std::optional<NpcNavigationVector> route_face(
      const NpcNavigationVector&, const NpcNavigationVector&) const override {
    return std::nullopt;
  }
  bw::game::CollisionRouteGround ground(
      const bw::game::CollisionRouteGroundRequest& request) const override {
    return {request.point, true};
  }
  bool support(const NpcNavigationVector&, const NpcNavigationVector&, float,
               float, float, float) const override {
    return pass;
  }
  bool clear(const NpcNavigationVector& from, const NpcNavigationVector& to,
             float, float) const override {
    return traversable(from, to);
  }
  NpcNavigationVector initial_waypoint(const NpcNavigationVector&,
                                       const NpcNavigationVector& to,
                                       float) const override {
    return to;
  }
  std::array<bw::game::NpcCoverPoint, 2> points{
      {{{2, 0, 0}, {1, 0, 0}}, {{8, 0, 0}, {1, 0, 0}}}};
  bool pass{true}, hidden{true}, graph_segments{};
};
}  // namespace
void scene_navigation_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  const bw::formats::NavigationArchive archive({}, domain);
  // Values executed from the original heading-cap and quaternion-blend
  // routines, including their positive-axis exact-opposite convention.
  struct HeadingCase {
    float initial_heading, target_heading, seconds, expected;
  };
  const auto check_heading = [](const HeadingCase& observation) {
    const auto [initial_heading, target_heading, seconds, expected] =
        observation;
    bw::game::NpcSteeringHeading heading;
    heading.reset({std::sin(initial_heading), 0, std::cos(initial_heading)});
    const auto result =
        heading.advance({std::sin(target_heading), 0, std::cos(target_heading)},
                        {}, 0, true, seconds);
    require(std::abs(std::atan2(result[0], result[2]) - expected) < 2e-6F,
            "Route heading must match original capped spherical blend");
  };
  check_heading({0, .5F, 1.F / 60, .04188789635F});
  check_heading({0, -.5F, 1.F / 60, -.04188789635F});
  check_heading({0, std::numbers::pi_v<float>, 1.F / 60, .04188789635F});
  check_heading({0, -std::numbers::pi_v<float>, 1.F / 60, .04188789635F});
  check_heading({0, .01F, 1.F / 60, .003999999867F});
  check_heading({.4F, -.5F, 1.F / 30, .31622417859F});
  check_heading({.4F, -.5F, 0, .4F});

  bw::scene::StaticCollision empty_collision(domain);
  bw::game::NpcSteeringSensors sensors;
  const bw::game::NpcSteeringSettings settings{.cohesion_cosine = -.1F,
                                               .cohesion_squared = 33.8F,
                                               .separation_squared = 6.5F,
                                               .avoidance_threshold = 10.3F,
                                               .aggregate_weight = 1.9F,
                                               .cohesion_weight = .4F,
                                               .alignment_weight = 2,
                                               .separation_weight = 1.4F,
                                               .avoidance_weight = .19F};
  const bw::game::NpcSteeringRequest first{1, {}, {0, 0, 1}, settings};
  const bw::game::NpcSteeringRequest second{2, {0, 0, 1}, {1, 0, 0}, settings};
  require(sensors.refresh(first).direction(true) == NpcNavigationVector{},
          "New sensor cannot publish uncomputed neighbors");
  (void)sensors.refresh(second);
  sensors.advance(empty_collision);
  const auto completed = sensors.refresh(first);
  require(std::abs(completed.cohesion[2] - .4F) < 1e-6F &&
              std::abs(completed.other[0] - 2) < 1e-6F &&
              std::abs(completed.other[2] + 1.4F) < 1e-6F,
          "Completed neighbor sample preserves separately weighted terms");
  sensors.retire(2);
  require(sensors.refresh(first).other == completed.other,
          "Pending refresh preserves previous completed publication");
  sensors.advance(empty_collision);
  require(sensors.refresh(first).direction(true) == NpcNavigationVector{},
          "Retired neighbor must disappear at next service publication");
  Queries queries;
  bw::game::SourceRandom random;
  bw::game::NpcSceneRouteFollower route(archive, 0, queries,
                                        {random, queries, .25F, true}, domain);
  auto result = route.advance({}, {10, 0, 0}, 1, .1F, true);
  require(result.waypoint[0] == 10 && !result.blocked && route.has_route(),
          "Visible destination must remain direct");
  queries.pass = false;
  result = route.advance({}, {10, 0, 0}, 1, .1F, true);
  require(!result.blocked && result.waypoint == NpcNavigationVector{10, 0, 0},
          "Blocked direct route enters retained collision search without "
          "fabricating arrival");
  require(route.collision_route_active() && !route.pair_ready(),
          "Outer route enters before creating its worker pair");
  (void)route.advance({}, {10, 0, 0}, 1, .1F, true);
  require(!route.pair_ready() && random.draws() == 1,
          "Automatic side is drawn once when the pair is admitted");
  route.advance_jobs();
  (void)route.advance({}, {10, 0, 0}, 1, .1F, true);
  require(
      route.pair_ready() && random.draws() == 1,
      "Processed pair remains ready after requeue without another random side");
  queries.pass = true;
  result = route.advance({10, 0, 0}, {10, 0, 0}, 1, .1F, true);
  require(result.arrived && !route.has_route(),
          "Arrival must retire retained route");
  const bw::formats::NavigationArchive graph(navigation_fixture::archive(),
                                             domain);
  bw::game::NpcSceneRouteFollower routed(graph, 1, queries,
                                         {random, queries, .25F, true}, domain);
  queries.graph_segments = true;
  result = routed.advance({-1, 0, 0}, {2, 0, 0}, .01F, .1F, true);
  require(result.waypoint == NpcNavigationVector{0, 0, 0} && !result.blocked,
          "Blocked destination must traverse authored graph links");
  result = routed.advance({0, 0, 0}, {2, 0, 0}, .01F, .1F, true);
  require(result.waypoint == NpcNavigationVector{1, 0, 0} && !result.blocked,
          "Retained route must advance to the next authored vertex");
  result = routed.advance({1, 0, 0}, {2, 0, 0}, .01F, .1F, true);
  require(result.waypoint == NpcNavigationVector{2, 0, 0} && !result.blocked,
          "Visible final segment must return to direct following");
  queries.graph_segments = false;
  bw::game::NpcCoverSearch cover(queries);
  cover.request({.position = {},
                 .threat = {10, 0, 0},
                 .body_radius = .3F,
                 .body_height = 1});
  cover.advance(0);
  require(!cover.result().ready, "Pause must not execute pending search");
  cover.advance(.1F);
  require(cover.result().ready && cover.result().found &&
              cover.result().position[0] == 2,
          "Cover must preserve near-half threat projection and copied result");
  queries.hidden = false;
  cover.request({.position = {},
                 .threat = {10, 0, 0},
                 .body_radius = .3F,
                 .body_height = 1});
  cover.advance(.1F);
  require(cover.result().ready && !cover.result().found,
          "Visible point is not cover");
}
