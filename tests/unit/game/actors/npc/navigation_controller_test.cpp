// Public hysteresis contract: source distance gates, retained phases, admission
// and route/ground capability ownership. No production solver is replaced by
// these value-only test doubles. I.1 documents explicit inputs; MEM.9 retains
// fixed state with no allocation on advance.
#include "game/actors/npc/navigation_controller.hpp"

#include <limits>

#include "../../../../fixtures/camera_fixture.hpp"
namespace {
using bw::game::NpcNavigationController;
using bw::game::NpcNavigationFrame;
using bw::game::NpcNavigationQueries;
using bw::game::NpcNavigationRequest;
using bw::game::NpcNavigationVector;
using bw::game::NpcRouteFollower;
using bw::game::NpcRouteResult;
using camera_fixture::require;
class Queries final : public NpcNavigationQueries {
 public:
  bool visible(const NpcNavigationVector&) const override { return visibility; }
  NpcNavigationVector project(const NpcNavigationVector&,
                              const NpcNavigationVector& point,
                              bool) const override {
    return point;
  }
  bool visibility{true};
};
class Route final : public NpcRouteFollower {
 public:
  NpcRouteResult advance(const NpcNavigationVector&,
                         const NpcNavigationVector& destination, float arrival,
                         float, bool trace) override {
    ++calls;
    last_destination = destination;
    last_arrival = arrival;
    last_trace = trace;
    active = true;
    return {destination, false, false};
  }
  void reset() override { active = false; }
  bool has_route() const override { return active; }
  NpcNavigationVector last_destination{};
  float last_arrival{};
  unsigned calls{};
  bool active{}, last_trace{};
};
}  // namespace
void navigation_controller_tests() {
  Queries queries;
  Route route;
  NpcNavigationController controller(queries, route);
  NpcNavigationRequest request{.destination = {0, 0, 10},
                               .approach_start_squared = 9,
                               .approach_stop_squared = 4,
                               .retreat_start_squared = 1,
                               .retreat_distance = 3};
  NpcNavigationFrame frame{
      .position = {}, .delta_seconds = 0.1F, .retreat_arrival_squared = 0.2F};
  auto result = controller.advance(request, frame);
  require(!result.arrived && !result.moving && route.calls == 0,
          "Idle transition moved before next source interval");
  result = controller.advance(request, frame);
  require(result.moving && result.direction[2] == 1 && route.calls == 1 &&
              route.last_arrival == 4.25F,
          "Approach lost source bias or retained route");
  frame.position = {0, 0, 8};
  result = controller.advance(request, frame);
  require(result.arrived && !result.moving,
          "Approach failed inner stop threshold");
  frame.position = {0, 0, 7.5F};
  result = controller.advance(request, frame);
  require(result.arrived && !result.moving, "Stop/start hysteresis oscillated");
  controller.reset();
  frame.position = {0, 0, 9.5F};
  result = controller.advance(request, frame);
  require(!result.moving && !result.arrived,
          "Retreat transition skipped retained phase");
  result = controller.advance(request, frame);
  require(result.moving && result.direction[2] == -1 &&
              route.last_destination[2] == 7 && route.last_arrival == 0.2F,
          "Retreat did not preserve source radius and route arrival");
  const unsigned calls = route.calls;
  frame.delta_seconds = 0;
  result = controller.advance(request, frame);
  require(route.calls == calls && !result.moving,
          "Paused navigation advanced route");
  frame.delta_seconds = std::numeric_limits<float>::quiet_NaN();
  bool rejected = false;
  try {
    (void)controller.advance(request, frame);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && route.calls == calls, "Invalid frame mutated navigation");
}
