// Unit test: public contracts of bw/rendering/scene/detail_level.hpp. Pure
// arithmetic; no device.
#include "bw/rendering/scene/detail_level.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

namespace s = bw::scene;
using bw::testing::require;

// (detail_level.hpp, contracts 1 to 3): the projected radius squared with
// its distance floor; the level scanned from the last down against half
// the screen size squared, a tie selecting; one level without a test; the
// older level fading out with the transition and the newer fading in with
// the transition minus one.
void choice_and_dither() {
  require(s::projected_radius_squared(100.0F, 2.0F, 1.5F) == 0.09F &&
              s::projected_radius_squared(0.25F, 2.0F, 1.5F) == 9.0F,
          "the projected radius squared, distance floored at 1");
  struct Row {
    std::vector<float> sizes;
    float projected;
    std::uint32_t level;
  };
  const std::array rows{Row{{1.0F, 0.5F, 0.25F}, 0.01F, 2}, Row{{1.0F, 0.5F, 0.25F}, 0.0625F, 1},
                        Row{{1.0F, 0.5F, 0.25F}, 0.3F, 0}, Row{{1.0F}, 100.0F, 0},
                        Row{{}, 1.0F, 0}};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    require(s::select_detail_level(rows[i].sizes, rows[i].projected) == rows[i].level, i,
            "the level chosen");
  }
  const s::DetailTransition one = s::detail_transition(1, 1, 0.3F);
  require(one.first_level == 1U && one.second_level == 1U && one.first_weight == 0.0F &&
              one.second_weight == 0.0F,
          "equal levels draw that level alone, weight 0");
  const s::DetailTransition two = s::detail_transition(2, 1, 0.25F);
  const float older = two.first_level == 1U ? two.first_weight : two.second_weight;
  const float newer = two.first_level == 2U ? two.first_weight : two.second_weight;
  require(((two.first_level == 1U && two.second_level == 2U) ||
           (two.first_level == 2U && two.second_level == 1U)) &&
              older == 0.25F && newer == -0.75F,
          "the older level fades out, the newer fades in");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{Scenario{"choice_and_dither", &choice_and_dither}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
