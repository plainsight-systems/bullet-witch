// Paired routing exercises actor/world phase separation and delayed readiness.
// Both I.1 and MEM.9 preserve explicit fixed-state ownership in the fixture.
#include "game/actors/npc/collision_route_pair.hpp"

#include <stdexcept>
#include <type_traits>

#include "../../../../fixtures/camera_fixture.hpp"
namespace {
using Vector = bw::game::CollisionRouteVector;
class ClearTerrain final : public bw::game::CollisionRoutePairQueries {
 public:
  bw::game::CollisionRouteGround ground(
      const bw::game::CollisionRouteGroundRequest& request) const override {
    const auto& point = request.point;
    return {point, true};
  }
  bool support(const Vector&, const Vector&, float, float, float,
               float) const override {
    return true;
  }
  bool clear(const Vector&, const Vector&, float, float) const override {
    return true;
  }
  Vector initial_waypoint(const Vector&, const Vector& destination,
                          float) const override {
    return destination;
  }
};
}  // namespace
void collision_route_pair_tests() {
  using bw::game::CollisionRoutePair;
  using camera_fixture::require;
  static_assert(!std::is_copy_constructible_v<CollisionRoutePair>);
  static_assert(!std::is_move_constructible_v<CollisionRoutePair>);
  ClearTerrain terrain;
  bw::game::CollisionRoutePairRequest request{{}, {0, 0, 8}, .5F, 1, false};
  CollisionRoutePair pair;
  require(!pair.sample().active, "New pair needs a caller-selected side");
  auto result = pair.advance_actor(request, terrain);
  require(result.active && !result.request_pair_ready &&
              result.waypoint == request.destination,
          "Pair publishes the initial clear waypoint while both jobs await "
          "world work");
  pair.advance_actor(request, terrain);
  require(pair.sample().waypoint == request.destination &&
              !pair.sample().request_pair_ready,
          "Repeated actor calls cannot execute the pending collision jobs");
  pair.advance_jobs(terrain);
  require(
      !pair.sample().request_pair_ready,
      "World-ready jobs do not admit pair following before actor consumption");
  result = pair.advance_actor(request, terrain);
  require(result.waypoint == request.position && result.request_pair_ready,
          "Pair following remains ready after publishing current and requeuing "
          "both");
  pair.advance_jobs(terrain);
  result = pair.advance_actor(request, terrain);
  const float step = .5F * 1.0099999904632568F;
  require(result.waypoint == Vector{0, 0, step},
          "Next world step supplies the original radius-scaled waypoint");
  const auto retained = result.waypoint;
  auto invalid = request;
  invalid.body_radius = 0;
  bool rejected = false;
  try {
    pair.advance_actor(invalid, terrain);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && pair.sample().waypoint == retained,
          "Invalid actor request leaves the retained pair unchanged");
  pair.cancel();
  pair.advance_jobs(terrain);
  require(!pair.sample().active && !pair.sample().request_pair_ready,
          "Cancel retires both requests before later world phase");

  request.delayed = true;
  pair.advance_actor(request, terrain);
  pair.advance_jobs(terrain);
  pair.advance_actor(request, terrain);
  for (int i = 0; i < 5; ++i) {
    pair.advance_jobs(terrain);
    result = pair.advance_actor(request, terrain);
    require(result.request_pair_ready == (i == 4) &&
                result.waypoint == request.position,
            "Source four-count delay admits following only at its negative "
            "transition");
  }
  pair.advance_actor(request, terrain);
  require(pair.sample().request_pair_ready,
          "Following readiness survives job requeue after delayed admission");
  pair.advance_jobs(terrain);
  result = pair.advance_actor(request, terrain);
  require(
      result.waypoint == Vector{0, 0, step},
      "Delayed pair resumes the same step without synthetic route completion");
}
