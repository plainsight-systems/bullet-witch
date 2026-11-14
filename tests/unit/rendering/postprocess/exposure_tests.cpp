// Unit test: the pure functions of bw/rendering/postprocess/exposure.hpp,
// one scenario per contract, each a table of rows.
#include "bw/rendering/postprocess/exposure.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {
namespace pp = bw::postprocess;
using bw::testing::require;
using Scenario = bw::testing::Scenario<void>;

bool near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }
bool near_relative(float a, float b) { return std::fabs(a - b) <= 1e-5F * std::fabs(b); }

// (exposure.hpp, contract 2): kInitialExposure is the factor of
// kInitialEv100.
void camera_relation() {
  require(near_relative(pp::exposure_from_ev100(pp::kInitialEv100), pp::kInitialExposure),
          "the initial factor is the factor of EV100 15");
}

// (exposure.hpp, contract 3): the two-bucket split of a sample and the
// black bucket that meters nothing.
void meter_buckets() {
  struct Row {
    float log_luminance;
    std::uint32_t first;
    std::uint32_t second;
    float first_weight;
    float second_weight;
  };
  const float step = (pp::kHistogramLogMax - pp::kHistogramLogMin) / 63.0F;
  const std::array rows{
      Row{-40.0F, 0, 1, 0.0F, 0.0F},                               // below the range: black
      Row{pp::kHistogramLogMin + 0.25F * step, 0, 1, 0.0F, 0.25F}, // bucket 0 meters nothing
      Row{pp::kHistogramLogMin + 10.25F * step, 10, 11, 0.75F, 0.25F},
      Row{pp::kHistogramLogMax, 63, 63, 1.0F, 0.0F},
      Row{60.0F, 63, 63, 1.0F, 0.0F}, // above the range: the top bucket
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const pp::BucketSplit split = pp::histogram_buckets(std::exp2(rows[i].log_luminance));
    require(split.first == rows[i].first && split.second == rows[i].second, i, "buckets");
    require(near(split.first_weight, rows[i].first_weight, 1e-3F) &&
                near(split.second_weight, rows[i].second_weight, 1e-3F),
            i, "weights");
  }
}

// (exposure.hpp, contract 4): the lowest tenth and the highest tenth of the
// mass are discarded and the rest averaged in log2; an empty histogram is
// the average at the range's lower end over kMiddleGrey.
void windowed_average() {
  std::array<float, pp::kHistogramBuckets> h{};
  require(pp::windowed_average_luminance(h, 4.0F) ==
              pp::average_luminance_at_ev100(4.0F) / pp::kMiddleGrey,
          "empty histogram");
  h[20] = 5.0F;
  require(near_relative(pp::windowed_average_luminance(h, pp::kDefaultMinEv100),
                        std::exp2(pp::bucket_log_luminance(20))),
          "one bucket");
  h[10] = 2.0F; // the low tenth (1 of 10) takes half of it
  h[20] = 6.0F;
  h[40] = 2.0F; // the high tenth takes half of it
  const float expected_log = (pp::bucket_log_luminance(10) + 6.0F * pp::bucket_log_luminance(20) +
                              pp::bucket_log_luminance(40)) /
                             8.0F;
  require(near_relative(pp::windowed_average_luminance(h, pp::kDefaultMinEv100),
                        std::exp2(expected_log)),
          "the window keeps 8 of 10");
}

// (exposure.hpp, contract 5): reset goes to the target, a zero step keeps
// the previous exposure, the speed is 3 stops a second when the scene got
// brighter and 1 when darker, the step is exponential (times M) within 1.5
// stops and linear past them, and the averages are clamped to the range.
// Average 0.18 at compensation 0 is a target of 1; previous 2 is the scene
// one stop brighter. At a sixtieth of a second M makes the exponential step
// d * speed / 90. A range with equal ends pins the exposure at 2^c / (1.2 *
// 2^ev).
void adaptation() {
  struct Row {
    float previous;
    float delta_seconds;
    bool reset;
    float expected;
  };
  const std::array rows{
      Row{2.0F, 1.0F / 60.0F, true, 1.0F},
      Row{2.0F, 0.0F, false, 2.0F},
      Row{2.0F, 1.0F / 60.0F, false, 2.0F * std::exp2(-1.0F / 30.0F)}, // up 1, exponential
      Row{0.5F, 1.0F / 60.0F, false, 0.5F * std::exp2(1.0F / 90.0F)},  // down 1, exponential
      Row{4.0F, 0.1F, false, 4.0F * std::exp2(-0.3F)},                 // up 2, linear
      Row{0.25F, 0.1F, false, 0.25F * std::exp2(0.1F)},                // down 2, linear
      Row{1e-12F, 0.0F, false,
          0.18F / pp::average_luminance_at_ev100(pp::kDefaultMaxEv100)}, // clamped
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const float adapted = pp::adapt_exposure(
        pp::AdaptationInputs{rows[i].previous, 0.18F, 0.0F, rows[i].delta_seconds, rows[i].reset});
    require(near_relative(adapted, rows[i].expected), i, "adapted exposure");
  }
  // Equal ends at EV100 3, compensation -2: the average, the previous
  // exposure and the step are far from that camera, and there is no reset.
  const float pinned = pp::adapt_exposure(
      pp::AdaptationInputs{pp::kInitialExposure, 50.0F, -2.0F, 0.5F, false, 3.0F, 3.0F});
  require(near_relative(pinned, std::exp2(-2.0F) / (1.2F * std::exp2(3.0F))),
          "a range with equal ends pins the exposure");
}

constexpr std::array scenarios{
    Scenario{"camera_relation", camera_relation},
    Scenario{"meter_buckets", meter_buckets},
    Scenario{"windowed_average", windowed_average},
    Scenario{"adaptation", adaptation},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
