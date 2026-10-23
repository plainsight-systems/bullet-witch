// Decode the retained customization tree into stage environment values. The
// contract and source provenance live in stage_environment.hpp. Tree traversal
// replaces mutable x86 relocation with checked immutable offsets; temporary
// metadata is domain-owned and unwinds on failure. Design/implementation:
// R.2 (#rr-use-ptr) bounds borrowed arrays; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// all traversal storage. No renderer policy or animation sampling occurs here.
// Fog fields preserve authored enable bytes and finite scalars independently;
// view composition owns disabled defaults and coordinate conversion. The fog
// extension reread R.2 and MEM.6 in both phases and adds no allocation.
#include "bw/formats/stage_environment.hpp"

#include <bit>
#include <cmath>

#include "bw/formats/customization.hpp"
#include "reader.hpp"

namespace bw::formats {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
constexpr std::uint64_t kWordBytes = 4;
constexpr std::uint64_t kEnvironmentBytes = 0x160;
constexpr std::uint64_t kAmbientColor = 0;
constexpr std::uint64_t kSkyYaw = 0xc0, kSkyHeight = 0xc4;
constexpr std::uint64_t kLightDiffuse = 0xc8, kLightSpecular = 0xd8;
constexpr std::uint64_t kLightVector = 0xf4;
constexpr std::uint64_t kShadowCull = 0xe8, kShadowRadius = 0xec;
constexpr std::uint64_t kShadowFar = 0x100, kShadowMinHeight = 0x108;
constexpr std::uint64_t kShadowMaxHeight = 0x10c, kShadowAlignment = 0x124;
constexpr std::uint64_t kShadowNearExtension = 0x128;
constexpr std::uint64_t kShadowSetup = 0x111, kShadowCasters = 0x113;
constexpr std::uint64_t kShadowDepthBias = 0x12c, kShadowSlopeBias = 0x130;
constexpr std::uint64_t kSurfaceFog = 0x24, kSkyFog = 0x50;
constexpr std::uint64_t kFogColor = 0x7c;
constexpr std::uint64_t kToneEnabled = 0x134, kExposure = 0x138, kGamma = 0x13c;
constexpr std::uint64_t kBloomEnabled = 0x140, kBloomExposure = 0x144;
constexpr std::uint64_t kBloomThreshold = 0x148, kBloomSoftKnee = 0x14c;
constexpr std::uint64_t kHistory = 0x150, kAdaptation = 0x154;
constexpr std::uint64_t kMinLuminance = 0x158, kMaxLuminance = 0x15c;
float scalar(const Reader& reader, std::uint64_t offset) {
  const float value = std::bit_cast<float>(reader.u32(offset));
  if (!std::isfinite(value)) {
    malformed("Nonfinite stage environment setting");
  }
  return value;
}
// Both blocks share their scalar layout; the surface block packs its distance
// enable after the independent area-override byte, which is not a fog flag.
struct FogLayout {
  std::uint64_t base{}, distance_enable_offset{};
};
StageFog fog(const Reader& reader, FogLayout layout) {
  constexpr std::uint64_t kDistanceStart = 4, kDistanceEnd = 8, kDensity = 12;
  constexpr std::uint64_t kGateEnabled = 16, kGateStart = 20, kGateEnd = 24;
  constexpr std::uint64_t kHeightEnabled = 28, kHeightStart = 32;
  constexpr std::uint64_t kHeightEnd = 36, kHeightOpacity = 40;
  const auto enabled = [&](std::uint64_t offset) {
    constexpr std::uint32_t kByteMask = 255;
    return (reader.u32(layout.base + offset) & kByteMask) != 0;
  };
  const auto value = [&](std::uint64_t offset) {
    return scalar(reader, layout.base + offset);
  };
  return {.distance_enabled = enabled(layout.distance_enable_offset),
          .height_gate_enabled = enabled(kGateEnabled),
          .height_enabled = enabled(kHeightEnabled),
          .distance_start = value(kDistanceStart),
          .distance_end = value(kDistanceEnd),
          .density = value(kDensity),
          .height_gate = {value(kGateStart), value(kGateEnd)},
          .height_range = {value(kHeightStart), value(kHeightEnd)},
          .height_opacity = value(kHeightOpacity)};
}
}  // namespace
StageEnvironment StageEnvironment::parse(
    std::span<const std::uint8_t> customization, std::string_view stage,
    const std::shared_ptr<memory::Domain>& domain) {
  const auto tree = CustomizationDefinition::parse(customization, domain);
  const std::array path{std::string_view("witch"), std::string_view("stage"),
                        stage};
  const auto payload = tree.lookup(path);
  const Reader reader{payload};
  reader.range(0, kEnvironmentBytes);
  const auto value = [&](std::uint64_t field) { return scalar(reader, field); };
  StageEnvironment result;
  for (std::size_t i = 0; i < result.ambient_color.size(); ++i) {
    result.ambient_color.at(i) = value(kAmbientColor + i * kWordBytes);
  }
  result.sky_yaw_degrees = value(kSkyYaw);
  result.sky_height = value(kSkyHeight);
  for (std::size_t i = 0; i < result.light_diffuse.size(); ++i) {
    result.light_diffuse.at(i) = value(kLightDiffuse + i * kWordBytes);
    result.light_specular.at(i) = value(kLightSpecular + i * kWordBytes);
  }
  for (std::size_t i = 0; i < result.light_vector.size(); ++i) {
    result.light_vector.at(i) = value(kLightVector + i * kWordBytes);
  }
  // Preserve enable gates separately from receiver radius and raster bias.
  result.shadow = {
      .setup_enabled = payload[static_cast<std::size_t>(kShadowSetup)] != 0,
      .casters_enabled = payload[static_cast<std::size_t>(kShadowCasters)] != 0,
      .cull_selector = reader.u32(kShadowCull),
      .receiver_radius = value(kShadowRadius),
      .raster_depth_bias = value(kShadowDepthBias),
      .raster_slope_bias = value(kShadowSlopeBias),
      .far_distance = value(kShadowFar),
      .minimum_height = value(kShadowMinHeight),
      .maximum_height = value(kShadowMaxHeight),
      .alignment_threshold = value(kShadowAlignment),
      .parallel_near_extension = value(kShadowNearExtension)};
  result.surface_fog = fog(reader, {kSurfaceFog, 1});
  result.sky_fog = fog(reader, {kSkyFog, 0});
  for (std::size_t i = 0; i < result.fog_color.size(); ++i) {
    result.fog_color.at(i) = value(kFogColor + i * kWordBytes);
  }
  auto& tone = result.tone_mapping;
  tone.enabled = payload[static_cast<std::size_t>(kToneEnabled)] != 0;
  tone.bloom_enabled = payload[static_cast<std::size_t>(kBloomEnabled)] != 0;
  tone.exposure_key = value(kExposure);
  tone.gamma = value(kGamma);
  tone.bloom_exposure_key = value(kBloomExposure);
  tone.bloom_threshold = value(kBloomThreshold);
  tone.bloom_soft_knee = value(kBloomSoftKnee);
  tone.history_window = std::bit_cast<std::int32_t>(reader.u32(kHistory));
  tone.adaptation_slew = value(kAdaptation);
  tone.minimum_luminance = value(kMinLuminance);
  tone.maximum_luminance = value(kMaxLuminance);
  if (tone.gamma <= 0 || tone.minimum_luminance <= 0 ||
      tone.maximum_luminance < tone.minimum_luminance ||
      tone.adaptation_slew < 0) {
    malformed("Invalid authored tone mapping range");
  }
  return result;
}
}  // namespace bw::formats
