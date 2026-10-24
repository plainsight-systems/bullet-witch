// In-process original exposure contracts using authored scalar samples, inside
// the existing resource scenario. Exercise temporal boundaries and invariant
// preservation rather than comparing the implementation with a copied formula.
// Both MCP phases: I.1 (#ri-explicit) keeps elapsed input observable; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds history
// storage.
#include "bw/render/world_output.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void near(float actual, float expected, const char* message) {
  constexpr float kTolerance = 0.00001F;
  require(std::abs(actual - expected) < kTolerance, message);
}
}  // namespace
void world_output_tests() {
  using bw::render::ExposureController;
  using bw::render::ExposureParameters;
  constexpr float interval = 1.0F / 30.0F;
  const auto bounds = bw::render::exposure_output_bounds({64, 1, 2, 1});
  near(bounds.measurement_floor, .001F,
       "Shared measurement floor is original policy");
  near(bounds.minimum_luminance.value_or(0), 2,
       "Shared lower bound preserves authored value");
  near(bounds.maximum_luminance.value_or(0), 2.001F,
       "Shared bounds preserve minimum separation");
  const auto disabled = bw::render::exposure_output_bounds({64, 1, {}, {}});
  require(!disabled.minimum_luminance && !disabled.maximum_luminance,
          "Absent authored bounds remain absent for every backend");
  ExposureController controller;
  ExposureParameters parameters{2, 1, std::nullopt, std::nullopt};
  near(controller.advance({std::log(2.0F), 0}, parameters), 2,
       "Initial adaptation uses real measured luminance without history");
  require(controller.sample_count() == 0 && !controller.history_ready(),
          "Zero time does not manufacture a sample");
  controller.advance({std::log(2.0F), interval / 2}, parameters);
  require(controller.sample_count() == 0, "Subinterval retains sample clock");
  controller.advance({std::log(2.0F), interval / 2}, parameters);
  require(controller.sample_count() == 1, "Exact interval inserts one sample");
  near(controller.advance({std::log(4.0F), interval}, parameters), 2 + interval,
       "Full history slews toward average rather than snapping to measurement");
  require(controller.history_ready(), "Second sample fills two-slot history");
  near(controller.advance({std::log(4.0F), interval}, parameters),
       2 + 2 * interval, "Ring replacement preserves bounded adaptation");
  parameters.minimum_luminance = 10;
  near(controller.advance({std::log(4.0F), 0}, parameters), 10,
       "Output minimum is applied after adaptation");
  near(controller.unconstrained_luminance(), 2 + 2 * interval,
       "Output clamp never contaminates temporal state");
  parameters.minimum_luminance.reset();
  near(controller.advance({std::log(4.0F), 0}, parameters), 2 + 2 * interval,
       "Removing output clamp exposes retained state");
  controller.reset();
  parameters.history_window = 64;
  controller.advance({0, 5}, parameters);
  require(controller.sample_count() == 1 && !controller.history_ready(),
          "Slow frame inserts one sample and drops excess time");
  controller.advance({0, interval / 2}, parameters);
  require(controller.sample_count() == 1,
          "Dropped time is not carried forward");
  parameters.history_window = 3;
  controller.advance({std::log(2.0F), interval / 2}, parameters);
  require(controller.sample_count() == 1 && !controller.history_ready(),
          "Window change resets samples but preserves external sample clock");
  const auto retained = controller.unconstrained_luminance();
  bool rejected = false;
  try {
    controller.advance({std::numeric_limits<float>::quiet_NaN(), interval},
                       parameters);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && controller.sample_count() == 1,
          "Invalid measurement cannot mutate history");
  near(controller.unconstrained_luminance(), retained,
       "Invalid measurement preserves adaptation");
  controller.reset();
  parameters = {2, 0.0005F, std::nullopt, std::nullopt};
  controller.advance({0, interval}, parameters);
  near(controller.advance({std::log(3.0F), interval}, parameters),
       1 + 0.005F * interval, "Authored slew respects original setter minimum");
  controller.reset();
  parameters = {2, 1, 2, 1};
  near(controller.advance({std::log(10.0F), 0}, parameters), 2.001F,
       "Inverted enabled bounds preserve original minimum separation");
  controller.reset();
  parameters = {2, 1, std::nullopt, std::nullopt};
  near(controller.advance({-100, 0}, parameters), 0.001F,
       "Dark reduction uses original luminance floor");
}
