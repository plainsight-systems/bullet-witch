// Decode fixed authored fields after the generic tree has bounded each payload.
// Failure publishes no partial settings. See player_camera.hpp for ownership
// and costs. Both MCP phases: I.1 (ri-explicit) makes ordinary look rate/limit
// contracts explicit; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps results fixed.
// Original tree boundary: R.2 (#rr-use-ptr) bounds source views; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) keeps
// tree allocation in its injected owner; this decoder allocates nothing.
#include "bw/formats/player_camera.hpp"

#include <bit>
#include <cmath>
#include <numbers>

#include "reader.hpp"
namespace bw::formats {
namespace {
constexpr std::uint64_t kPresetBytes = 0x1c;
constexpr std::uint64_t kWordBytes = 4, kOffsetField = 4;
constexpr std::uint64_t kPitchField = 0x10, kFovField = 0x18;
constexpr std::uint64_t kSpeedField = 0x1dc;
constexpr std::uint64_t kLookRateField = 0x1a8, kPitchLimitField = 0x1b4;
constexpr float kDegreesToRadians = std::numbers::pi_v<float> / 180.0F;
float scalar(const resources::detail::Reader& reader, std::uint64_t offset) {
  const auto value = std::bit_cast<float>(reader.u32(offset));
  if (!std::isfinite(value)) {
    resources::detail::malformed("Nonfinite player camera setting");
  }
  return value;
}
}  // namespace
PlayerCameraSettings PlayerCameraSettings::parse_initial(
    const CustomizationDefinition& tree) {
  constexpr std::array behind_path{
      std::string_view("witch"), std::string_view("actor"),
      std::string_view("pw0"), std::string_view("behind_camera")};
  constexpr std::array general_path{std::string_view("witch"),
                                    std::string_view("general"),
                                    std::string_view("camera")};
  const resources::detail::Reader behind{tree.lookup(behind_path)};
  const resources::detail::Reader general{tree.lookup(general_path)};
  PlayerCameraSettings result;
  constexpr std::array<std::uint64_t, 4> indices{2, 1, 14, 13};
  constexpr std::uint64_t kTargetHorizontalRate = 0x1ac;
  constexpr std::uint64_t kTargetVerticalRate = 0x1b0;
  constexpr std::uint64_t kAutomaticTargetingFlag = 0x14;
  constexpr std::uint32_t kByteMask = 0xffU;
  for (std::size_t preset = 0; preset < indices.size(); ++preset) {
    const auto base = indices.at(preset) * kPresetBytes;
    auto& value = result.contexts.at(preset);
    value.height = scalar(behind, base);
    for (std::size_t i = 0; i < value.offset.size(); ++i) {
      value.offset.at(i) = scalar(behind, base + kOffsetField + i * kWordBytes);
    }
    for (std::size_t i = 0; i < value.pitch_coefficients.size(); ++i) {
      value.pitch_coefficients.at(i) =
          scalar(behind, base + kPitchField + i * kWordBytes);
    }
    value.vertical_fov_radians =
        scalar(behind, base + kFovField) * kDegreesToRadians;
    value.look_radians_per_second = {
        scalar(behind,
               preset % 2 == 0 ? kLookRateField : kTargetHorizontalRate) *
            kDegreesToRadians,
        scalar(behind, preset % 2 == 0 ? kLookRateField : kTargetVerticalRate) *
            kDegreesToRadians};
    if (value.vertical_fov_radians <= 0 ||
        value.vertical_fov_radians >= std::numbers::pi_v<float> ||
        value.look_radians_per_second[0] <= 0 ||
        value.look_radians_per_second[1] <= 0) {
      resources::detail::malformed("Invalid player camera context preset");
    }
  }
  const auto& initial = result.contexts.front();
  result.height = initial.height;
  result.offset = initial.offset;
  result.pitch_coefficients = initial.pitch_coefficients;
  result.vertical_fov_radians = initial.vertical_fov_radians;
  result.automatic_targeting_camera =
      (general.u32(kAutomaticTargetingFlag) & kByteMask) != 0;
  result.transition_speed = scalar(behind, kSpeedField);
  result.look_radians_per_second =
      scalar(behind, kLookRateField) * kDegreesToRadians;
  result.pitch_limit_radians =
      scalar(behind, kPitchLimitField) * kDegreesToRadians;
  result.near_plane = scalar(general, 0);
  result.far_plane = scalar(general, kWordBytes);
  if (result.vertical_fov_radians <= 0 ||
      result.vertical_fov_radians >= std::numbers::pi_v<float> ||
      result.near_plane <= 0 || result.far_plane <= result.near_plane ||
      result.transition_speed < 0 || result.look_radians_per_second <= 0 ||
      result.pitch_limit_radians <= 0 ||
      result.pitch_limit_radians >= std::numbers::pi_v<float> / 2) {
    resources::detail::malformed("Invalid initial player camera range");
  }
  return result;
}
}  // namespace bw::formats
