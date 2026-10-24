// Original sampled luminance adaptation, independent of native image ownership.
// Validate the full input before mutation, retain unclamped temporal state, and
// apply output bounds last. The header records the algorithm and source map.
// Design/implementation I.1 (#ri-explicit) exposes elapsed time and settings;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps the
// per-frame path bounded with inline history rather than heap-backed samples.
#include "bw/render/world_output.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bw::render {
namespace {
constexpr float kLuminanceFloor = 0.001F;
constexpr float kSampleInterval = 1.0F / 30.0F;
constexpr float kMinimumSlew = 0.005F, kMaximumSlew = 100.0F;
constexpr float kMaximumLowerBound = 63.999F;
constexpr float kMinimumUpperBound = 0.002F, kMaximumUpperBound = 64.0F;
void require_finite(float value) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument("Exposure input must be finite");
  }
}
}  // namespace
// Normalize optional authored bounds once for either CPU history output or
// the first actual GPU reduction. No temporal state crosses this policy API.
ExposureOutputBounds exposure_output_bounds(
    const ExposureParameters& parameters) {
  auto lower = parameters.minimum_luminance;
  auto upper = parameters.maximum_luminance;
  if (lower) {
    require_finite(*lower);
    *lower = std::clamp(*lower, kLuminanceFloor, kMaximumLowerBound);
  }
  if (upper) {
    require_finite(*upper);
    *upper = std::clamp(*upper, kMinimumUpperBound, kMaximumUpperBound);
    if (lower) {
      *upper = std::max(*upper, *lower + kLuminanceFloor);
    }
  }
  return {kLuminanceFloor, lower, upper};
}
float ExposureController::advance(ExposureSample sample,
                                  const ExposureParameters& parameters) {
  require_finite(sample.log_average_luminance);
  require_finite(sample.elapsed_seconds);
  require_finite(parameters.adaptation_slew);
  if (sample.elapsed_seconds < 0) {
    throw std::invalid_argument("Exposure elapsed time must be nonnegative");
  }
  const auto bounds = exposure_output_bounds(parameters);
  // The original readback promotes the log scalar for exp, then stores float.
  const auto measured = static_cast<float>(
      std::exp(static_cast<double>(sample.log_average_luminance)));
  require_finite(measured);
  const auto current = std::max(measured, bounds.measurement_floor);
  const auto window = static_cast<std::size_t>(
      std::clamp(parameters.history_window, 1,
                 static_cast<std::int32_t>(kMaximumHistory)));
  const auto slew =
      std::clamp(parameters.adaptation_slew, kMinimumSlew, kMaximumSlew);
  const auto elapsed = elapsed_ + sample.elapsed_seconds;
  const auto maximum_change = slew * sample.elapsed_seconds;
  require_finite(elapsed);
  require_finite(maximum_change);
  // Changing the native sample-count setting resets temporal samples but not
  // the separately owned sampling clock. Reapplying the same count is inert.
  if (window != window_) {
    history_.fill(0);
    window_ = window;
    next_ = 0;
    filled_ = false;
    adapted_ = 0;
  }
  elapsed_ = elapsed;
  if (elapsed_ >= kSampleInterval) {
    elapsed_ = 0;
    history_.at(next_) = current;
    ++next_;
    if (next_ == window_) {
      next_ = 0;
      filled_ = true;
    }
  }
  if (filled_) {
    float sum = 0;
    for (std::size_t index = 0; index < window_; ++index) {
      sum += history_.at(index);
    }
    const auto target = sum / static_cast<float>(window_);
    adapted_ += std::clamp(target - adapted_, -maximum_change, maximum_change);
  } else {
    adapted_ = current;
  }
  // Output bounds do not change retained adaptation or future slew behavior.
  auto output = adapted_;
  const auto& lower = bounds.minimum_luminance;
  const auto& upper = bounds.maximum_luminance;
  if (lower) output = std::max(output, *lower);
  if (upper) output = std::min(output, *upper);
  return output;
}
void ExposureController::reset() noexcept {
  history_.fill(0);
  window_ = 0;
  next_ = 0;
  filled_ = false;
  elapsed_ = 0;
  adapted_ = 0;
}
std::size_t ExposureController::sample_count() const noexcept {
  return filled_ ? window_ : next_;
}
}  // namespace bw::render
