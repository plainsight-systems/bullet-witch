// Decode fixed authored fields after the generic tree has bounded each payload.
// Failure publishes no partial settings. See player_camera.hpp for ownership
// and costs. Both MCP phases: R.2 (#rr-use-ptr) bounds source views; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) keeps
// tree allocation in its injected owner; this decoder allocates nothing.
#include "bw/formats/player_camera.hpp"

#include <bit>
#include <cmath>
#include <numbers>

#include "reader.hpp"
namespace bw::formats {
namespace {
constexpr std::uint64_t kPresetBytes = 0x1c, kInitialPreset = 2;
constexpr std::uint64_t kWordBytes = 4, kOffsetField = 4;
constexpr std::uint64_t kPitchField = 0x10, kFovField = 0x18;
constexpr std::uint64_t kSpeedField = 0x1dc;
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
  constexpr auto base = kInitialPreset * kPresetBytes;
  PlayerCameraSettings result;
  result.height = scalar(behind, base);
  for (std::size_t i = 0; i < result.offset.size(); ++i) {
    result.offset.at(i) = scalar(behind, base + kOffsetField + i * kWordBytes);
  }
  for (std::size_t i = 0; i < result.pitch_coefficients.size(); ++i) {
    result.pitch_coefficients.at(i) =
        scalar(behind, base + kPitchField + i * kWordBytes);
  }
  result.vertical_fov_radians =
      scalar(behind, base + kFovField) * kDegreesToRadians;
  result.transition_speed = scalar(behind, kSpeedField);
  result.near_plane = scalar(general, 0);
  result.far_plane = scalar(general, kWordBytes);
  if (result.vertical_fov_radians <= 0 ||
      result.vertical_fov_radians >= std::numbers::pi_v<float> ||
      result.near_plane <= 0 || result.far_plane <= result.near_plane ||
      result.transition_speed < 0) {
    resources::detail::malformed("Invalid initial player camera range");
  }
  return result;
}
}  // namespace bw::formats
