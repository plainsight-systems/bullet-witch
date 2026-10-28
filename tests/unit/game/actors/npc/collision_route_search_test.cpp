// Exercise copied route job lifecycle and ordered wall-follow observations.
// Core I.1 distinguishes ready from complete; MEM.9 keeps fixtures bounded.
#include "game/actors/npc/collision_route_search.hpp"

#include <stdexcept>
#include <type_traits>

#include "../../../../fixtures/camera_fixture.hpp"
namespace {
using Vector = bw::game::CollisionRouteVector;
class Terrain final : public bw::game::CollisionRouteQueries {
 public:
  bool wall{}, reject_ground{};
  bw::game::CollisionRouteGround ground(
      const bw::game::CollisionRouteGroundRequest& request) const override {
    const auto& point = request.point;
    auto result = point;
    result[1] = 0;
    return {result, !reject_ground};
  }
  bool support(const Vector& point, const Vector&, float, float, float,
               float) const override {
    return !wall || point[2] < 1.5F || std::abs(point[0]) >= 1.5F;
  }
  bool clear(const Vector&, const Vector& point, float, float) const override {
    return !wall || point[2] < 1.5F || std::abs(point[0]) >= 1.5F;
  }
};
bw::game::CollisionRouteRequest request(int side = 1) {
  return {{0, 0, 0}, {0, 0, 8}, {2.25F, 1, 2, .3F, .99F, .9F, 512, side}};
}
}  // namespace
void collision_route_search_tests() {
  using bw::game::CollisionRouteSearch;
  using camera_fixture::require;
  static_assert(!std::is_copy_constructible_v<CollisionRouteSearch>);
  static_assert(!std::is_move_constructible_v<CollisionRouteSearch>);
  Terrain terrain;
  CollisionRouteSearch search;
  search.request(request());
  require(search.sample().pending && !search.sample().ready,
          "Submission schedules work rather than claiming a finished route");
  search.advance(terrain);
  require(
      search.sample().ready && !search.sample().complete &&
          search.sample().output == Vector{},
      "First source job update is ready while initialization retains origin");
  search.advance(terrain);
  require(search.sample().output == Vector{},
          "Ready jobs do not run again before their pair owner requeues");
  search.requeue();
  search.advance(terrain);
  require(search.sample().output == Vector{0, 0, 1},
          "A requeued straight step publishes its supported destination");
  for (int i = 0; i < 6; ++i) {
    search.requeue();
    search.advance(terrain);
  }
  require(search.sample().complete && search.sample().output == Vector{0, 0, 7},
          "Arrival uses strict squared threshold and a clear target capsule");
  const auto completed = search.sample().output;
  auto invalid = request();
  invalid.config.side = 0;
  bool rejected = false;
  try {
    search.request(invalid);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && search.sample().output == completed,
          "Invalid replacement preserves the preceding route result");
  search.cancel();
  require(!search.sample().pending && !search.sample().ready &&
              !search.sample().complete,
          "Cancellation removes both scheduling and published completion");

  terrain.wall = true;
  std::array<Vector, 2> side_outputs{};
  for (std::size_t side = 0; side < side_outputs.size(); ++side) {
    search.request(request(side == 0 ? 1 : -1));
    for (int step = 0; step < 3; ++step) {
      if (step != 0) search.requeue();
      search.advance(terrain);
    }
    require(
        search.sample().boundary && search.sample().output == Vector{0, 0, 1},
        "Blocked straight step starts boundary work without publishing it");
    search.requeue();
    search.advance(terrain);
    side_outputs.at(side) = search.sample().output;
    require(search.sample().output[2] == 1 &&
                std::abs(search.sample().output[0]) == 1,
            "Boundary publishes the preceding lateral step around obstruction");
  }
  require(side_outputs[0][0] == -side_outputs[1][0],
          "Opposite retained sides explore opposite sides of an obstruction");

  terrain.wall = false;
  terrain.reject_ground = true;
  auto bounded = request();
  bounded.config.step_budget = 1;
  search.request(bounded);
  for (int step = 0; step < 3; ++step) {
    if (step != 0) search.requeue();
    search.advance(terrain);
  }
  require(
      search.sample().complete && search.sample().output == Vector{},
      "An unsupported boundary terminates without fabricated forward motion");
}
