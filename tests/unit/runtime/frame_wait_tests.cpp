// Unit test: public contract of bw::engine::requested_frame_wait in
// bw/runtime/engine.hpp, and of bw::engine::make_step in bw/runtime/step.hpp.
// Pure arithmetic; no clock, loop or engine instance.
#include "bw/runtime/engine.hpp"
#include "bw/runtime/step.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace {
using bw::engine::requested_frame_wait;

using bw::testing::require;

void near(float observed, float expected, float tolerance, std::string_view message) {
  require(std::abs(observed - expected) <= tolerance, message);
}

void run() {
  near(requested_frame_wait(0.0F, 0.004F), 0.0F, 0.0F, "a zero maximum never waits");
  near(requested_frame_wait(-1.0F, 0.004F), 0.0F, 0.0F, "a negative maximum never waits");
  near(requested_frame_wait(100.0F, 0.004F), 0.006F, 1e-6F,
       "the wait is the cap interval minus the frame's real time");
  near(requested_frame_wait(100.0F, 0.010F), 0.0F, 0.0F, "a frame exactly on budget waits zero");
  near(requested_frame_wait(100.0F, 0.020F), 0.0F, 0.0F,
       "an over-budget frame clamps to zero, never negative");
  near(requested_frame_wait(30.0F, 0.0F), 1.0F / 30.0F, 1e-6F,
       "no work waits the whole cap interval");
}

// (step.hpp, contract 1): a fixed step whatever the clock reads, not held;
// a measured step otherwise, held to the longest step when one is set.
// Static rows: the executable does not compile when one breaks.
using bw::engine::make_step;
using bw::engine::StepSettings;
static_assert(make_step(StepSettings{0.25, 0.0}, 1.0, 9.0) == 0.25);
static_assert(make_step(StepSettings{0.0, 0.0}, 1.0, 1.5) == 0.5);
static_assert(make_step(StepSettings{0.0, 0.1}, 1.0, 1.5) == 0.1);
static_assert(make_step(StepSettings{0.0, 0.1}, 1.0, 1.0625) == 0.0625);
static_assert(make_step(StepSettings{0.5, 0.1}, 1.0, 1.0) == 0.5);

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{Scenario{"frame_wait", run}};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
