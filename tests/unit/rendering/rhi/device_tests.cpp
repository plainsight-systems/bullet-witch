// Unit test: validate_device_description of bw/rendering/rhi/device.hpp,
// one scenario per CTest entry. Each scenario names the header declaration it
// asserts. Device itself is not referenced: it links the backend dispatch
// and the validation layer, which this header-only test does not take.
#include "bw/rendering/rhi/device.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstdint>

namespace {
using bw::rhi::DeviceDescription;
using bw::rhi::DeviceError;
using bw::testing::require;
using Scenario = bw::testing::Scenario<void>;

// (device.hpp, validate_device_description): DescriptorCapacityInvalid, then
// FramesInFlightInvalid outside 1..kMaxFramesInFlight, then
// TransientBudgetInvalid for zero; the defaults are valid.
void device_description_validation_named() {
  require(bw::rhi::validate_device_description(DeviceDescription{}) == DeviceError::None,
          "the defaults are valid");
  DeviceDescription d{};
  d.descriptors.storage = bw::shaders::kStorageDescriptors + 1U;
  d.frames_in_flight = 0;
  d.transient_bytes_per_frame = 0;
  require(bw::rhi::validate_device_description(d) == DeviceError::DescriptorCapacityInvalid,
          "descriptor capacities first");
  d = DeviceDescription{};
  d.frames_in_flight = 0;
  d.transient_bytes_per_frame = 0;
  require(bw::rhi::validate_device_description(d) == DeviceError::FramesInFlightInvalid,
          "zero frames in flight, before the budget");
  d.frames_in_flight = bw::rhi::kMaxFramesInFlight + 1U;
  require(bw::rhi::validate_device_description(d) == DeviceError::FramesInFlightInvalid,
          "more than kMaxFramesInFlight");
  d = DeviceDescription{};
  for (std::uint32_t frames = 1; frames <= bw::rhi::kMaxFramesInFlight; ++frames) {
    d.frames_in_flight = frames;
    require(bw::rhi::validate_device_description(d) == DeviceError::None, "1..kMax are valid");
  }
  d.transient_bytes_per_frame = 0;
  require(bw::rhi::validate_device_description(d) == DeviceError::TransientBudgetInvalid,
          "a zero transient budget");
}

constexpr std::array scenarios{
    Scenario{"device_description_validation_named", device_description_validation_named},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
