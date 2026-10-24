// Shared display-output policy and original luminance adaptation. Parameters
// are copied from authored stage data; target backends own HDR images,
// reduction and completed-frame readback. One exclusive caller advances the
// controller once per frame with finite log luminance and nonnegative elapsed
// seconds. Invalid values throw before changing state. No device, clock or
// global state is hidden here. Reset establishes a fresh scene; changing
// history length clears history/adaptation but preserves the original sampling
// clock. Algorithm: exponentiate log-average luminance with the original floor;
// insert at most one history value when the accumulated clock reaches 1/30
// second, dropping excess time. Before the ring fills use the current value;
// afterward average the ring and limit change by authored slew * elapsed time.
// Apply the original output bounds after retaining the unclamped adaptation
// state. Steady state uses at most 256 inline floats and O(history length)
// additions; no allocation, locks or payload copies. Both MCP phases consulted
// I.1
// (#ri-explicit) for explicit samples/settings and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) for fixed hot-path
// storage. Provenance and native setter ranges: world-output-adaptation.md.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace bw::render {
struct ExposureParameters {
  std::int32_t history_window{};
  float adaptation_slew{};
  std::optional<float> minimum_luminance;
  std::optional<float> maximum_luminance;
};
struct ExposureOutputBounds {
  float measurement_floor{};
  std::optional<float> minimum_luminance;
  std::optional<float> maximum_luminance;
};
// Prepare the original optional output clamps without changing history. Both
// the CPU controller and a target's first real GPU measurement use these
// normalized limits. Invalid bounds throw; no samples, clocks or allocations
// are involved. Both-phase I.1 exposes the policy; GPU.1 keeps the initial
// reduction on-device rather than forcing an extra synchronous readback.
ExposureOutputBounds exposure_output_bounds(
    const ExposureParameters& parameters);
struct WorldOutputParameters {
  // Authored whole-scene filter precedes luminance measurement, bloom and
  // display transfer. Mode 0 bypasses it; mode 2 multiplies RGB by nonnegative
  // global_color.rgb and preserves source alpha. Other modes remain explicit
  // unsupported inputs. The backend owns a persistent HDR intermediate;
  // this is not an exposure multiplier. I.11 / GPU.9 ownership and allocation
  // rules were read in both design and implementation phases.
  std::int32_t global_color_mode{};
  std::array<float, 4> global_color{1, 1, 1, 1};
  bool tone_mapping_enabled{};
  float exposure_key{};
  float gamma{};
  bool bloom_enabled{};
  float bloom_exposure_key{};
  float bloom_threshold{};
  float bloom_soft_knee{};
  ExposureParameters exposure;
};
struct ExposureSample {
  float log_average_luminance{};
  float elapsed_seconds{};
};
class ExposureController {
 public:
  // Returns bounded output luminance. A nonpositive authored history window
  // disables temporal use in the backend; its internal history still uses one
  // sample, matching the original setter. This method does not select that
  // backend display mode or invent a first-frame measurement.
  float advance(ExposureSample sample, const ExposureParameters& parameters);
  void reset() noexcept;
  std::size_t sample_count() const noexcept;
  bool history_ready() const noexcept { return filled_; }
  float unconstrained_luminance() const noexcept { return adapted_; }

 private:
  static constexpr std::size_t kMaximumHistory = 256;
  std::array<float, kMaximumHistory> history_{};
  std::size_t window_{}, next_{};
  bool filled_{};
  float elapsed_{}, adapted_{};
};
}  // namespace bw::render
