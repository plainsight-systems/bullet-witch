// Decode the retained customization tree into stage environment values. The
// contract and source provenance live in stage_environment.hpp. Tree traversal
// replaces mutable x86 relocation with checked immutable offsets; temporary
// metadata is domain-owned and unwinds on failure. Design/implementation:
// R.2 (#rr-use-ptr) bounds borrowed arrays; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// all traversal storage. No renderer policy or animation sampling occurs here.
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
