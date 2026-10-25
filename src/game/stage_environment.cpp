// Translate original environment channel meanings at the game compatibility
// boundary. Prepare retained region/curve inputs once, seed every supported
// channel from authored state, and map the engine's current snapshot back to
// typed fields. Numeric channel IDs are a file schema, never tuning values.
// The source enables AREA at stage activation; raw CSTM's disabled byte is not
// a runtime gate. Source map and initialization witness are in
// lighting-scene-producer-contract.md. Both MCP phases: I.1 (#ri-explicit)
// separates schema from engine lifetime; MEM.9 prepares owned data at loading.
#include "stage_environment.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace bw::game {
namespace {
constexpr std::size_t kToneEnabled = 0;
constexpr std::size_t kToneExposureKey = 1;
constexpr std::size_t kToneGamma = 2;
constexpr std::size_t kToneHistoryWindow = 3;
constexpr std::size_t kToneAdaptationSlew = 4;
constexpr std::size_t kToneMinimumLuminance = 5;
constexpr std::size_t kToneMaximumLuminance = 6;
constexpr std::size_t kToneBloomEnabled = 7;
constexpr std::size_t kToneBloomExposureKey = 8;
constexpr std::size_t kToneBloomThreshold = 9;
constexpr std::size_t kToneBloomSoftKnee = 10;
constexpr std::size_t kToneColorFilterMode = 147;
constexpr std::size_t kSurfaceDistanceEnabled = 31;
constexpr std::size_t kSurfaceDistanceStart = 32;
constexpr std::size_t kSurfaceDensity = 33;
constexpr std::size_t kSurfaceHeightGateEnabled = 34;
constexpr std::size_t kSurfaceHeightGateStart = 35;
constexpr std::size_t kSurfaceHeightGateEnd = 36;
constexpr std::size_t kSurfaceHeightRangeStart = 37;
constexpr std::size_t kSurfaceHeightEnabled = 38;
constexpr std::size_t kSurfaceHeightRangeEnd = 39;
constexpr std::size_t kSurfaceHeightOpacity = 40;
constexpr std::size_t kSkyDistanceEnabled = 41;
constexpr std::size_t kSkyDistanceStart = 42;
constexpr std::size_t kSkyDensity = 43;
constexpr std::size_t kSkyHeightGateEnabled = 44;
constexpr std::size_t kSkyHeightGateStart = 45;
constexpr std::size_t kSkyHeightGateEnd = 46;
constexpr std::size_t kSkyHeightEnabled = 47;
constexpr std::size_t kSkyHeightRangeStart = 48;
constexpr std::size_t kSkyHeightRangeEnd = 49;
constexpr std::size_t kSkyHeightOpacity = 50;
constexpr std::size_t kCharacterLightIntensity = 61;
constexpr std::size_t kEnemyLightIntensity = 62;
constexpr std::size_t kSkyYawDegrees = 72;
constexpr std::size_t kSkyHeight = 73;
constexpr std::size_t kShadowCullSelector = 76;
constexpr std::size_t kShadowReceiverRadius = 77;
constexpr std::size_t kShadowFarDistance = 81;
constexpr std::size_t kShadowMinimumHeight = 82;
constexpr std::size_t kShadowMaximumHeight = 83;
constexpr std::size_t kShadowMinimumProjectedSize = 85;
constexpr std::size_t kShadowAlignmentThreshold = 86;
constexpr std::size_t kShadowParallelNearExtension = 87;
constexpr std::size_t kShadowRasterDepthBias = 88;
constexpr std::size_t kShadowRasterSlopeBias = 89;
constexpr std::size_t kShadowCastersEnabled = 140;
constexpr std::size_t kColorRgb = 14;
constexpr std::size_t kFogRgb = 51;
constexpr std::size_t kAmbientRgb = 54;
constexpr std::size_t kClearRgb = 57;
constexpr std::size_t kCameraFar = 149;
constexpr std::size_t kLightVector = 78;
constexpr std::size_t kLightDiffuse = 90;
constexpr std::size_t kLightSpecular = 94;
constexpr std::uint32_t kColorModeVersion = 0x106;
constexpr std::uint32_t kSceneVersion = 0x102;
constexpr std::uint32_t kCasterVersion = 0x105;
constexpr float kBooleanThreshold = .5F;
// Visit the admitted source fields once for initialization and publication.
// Unrepresented channels remain outside this typed presentation contract.
template <typename Visit>
void fields(formats::StageEnvironment& value, std::uint32_t version,
            const Visit& visit) {
  auto& tone = value.tone_mapping;
  visit(kToneEnabled, tone.enabled);
  visit(kToneExposureKey, tone.exposure_key);
  visit(kToneGamma, tone.gamma);
  visit(kToneHistoryWindow, tone.history_window);
  visit(kToneAdaptationSlew, tone.adaptation_slew);
  visit(kToneMinimumLuminance, tone.minimum_luminance);
  visit(kToneMaximumLuminance, tone.maximum_luminance);
  visit(kToneBloomEnabled, tone.bloom_enabled);
  visit(kToneBloomExposureKey, tone.bloom_exposure_key);
  visit(kToneBloomThreshold, tone.bloom_threshold);
  visit(kToneBloomSoftKnee, tone.bloom_soft_knee);
  for (std::size_t component = 0; component < 3; ++component) {
    visit(kColorRgb + component, tone.color_filter.at(component));
  }
  if (version >= kColorModeVersion) {
    visit(kToneColorFilterMode, tone.color_filter_mode);
  }
  if (version < kSceneVersion) return;
  auto& surface = value.surface_fog;
  visit(kSurfaceDistanceEnabled, surface.distance_enabled);
  visit(kSurfaceDistanceStart, surface.distance_start);
  visit(kSurfaceDensity, surface.density);
  visit(kSurfaceHeightGateEnabled, surface.height_gate_enabled);
  visit(kSurfaceHeightGateStart, surface.height_gate[0]);
  visit(kSurfaceHeightGateEnd, surface.height_gate[1]);
  visit(kSurfaceHeightRangeStart, surface.height_range[0]);
  visit(kSurfaceHeightEnabled, surface.height_enabled);
  visit(kSurfaceHeightRangeEnd, surface.height_range[1]);
  visit(kSurfaceHeightOpacity, surface.height_opacity);
  auto& sky = value.sky_fog;
  visit(kSkyDistanceEnabled, sky.distance_enabled);
  visit(kSkyDistanceStart, sky.distance_start);
  visit(kSkyDensity, sky.density);
  visit(kSkyHeightGateEnabled, sky.height_gate_enabled);
  visit(kSkyHeightGateStart, sky.height_gate[0]);
  visit(kSkyHeightGateEnd, sky.height_gate[1]);
  visit(kSkyHeightEnabled, sky.height_enabled);
  visit(kSkyHeightRangeStart, sky.height_range[0]);
  visit(kSkyHeightRangeEnd, sky.height_range[1]);
  visit(kSkyHeightOpacity, sky.height_opacity);
  for (std::size_t component = 0; component < 3; ++component) {
    visit(kFogRgb + component, value.fog_color.at(component));
    visit(kAmbientRgb + component, value.ambient_color.at(component));
    visit(kClearRgb + component, value.clear_color.at(component));
    visit(kLightVector + component, value.light_vector.at(component));
  }
  visit(kCharacterLightIntensity, value.character_light_intensity);
  visit(kEnemyLightIntensity, value.enemy_light_intensity);
  visit(kSkyYawDegrees, value.sky_yaw_degrees);
  visit(kSkyHeight, value.sky_height);
  auto& shadow = value.shadow;
  visit(kShadowCullSelector, shadow.cull_selector);
  visit(kShadowReceiverRadius, shadow.receiver_radius);
  visit(kShadowFarDistance, shadow.far_distance);
  visit(kShadowMinimumHeight, shadow.minimum_height);
  visit(kShadowMaximumHeight, shadow.maximum_height);
  visit(kShadowMinimumProjectedSize, shadow.minimum_projected_size);
  visit(kShadowAlignmentThreshold, shadow.alignment_threshold);
  visit(kShadowParallelNearExtension, shadow.parallel_near_extension);
  visit(kShadowRasterDepthBias, shadow.raster_depth_bias);
  visit(kShadowRasterSlopeBias, shadow.raster_slope_bias);
  for (std::size_t component = 0; component < 4; ++component) {
    visit(kLightDiffuse + component, value.light_diffuse.at(component));
    visit(kLightSpecular + component, value.light_specular.at(component));
  }
  if (version >= kCasterVersion) {
    visit(kShadowCastersEnabled, shadow.casters_enabled);
  }
}
std::span<const std::uint8_t> input_bytes(const content::StageDefinition& stage,
                                          const content::StageInput& input) {
  if (!input.archive) {
    throw std::invalid_argument("Missing stage input archive");
  }
  const auto& archive = stage.archives.at(*input.archive);
  std::optional<std::size_t> record;
  for (std::size_t index = 0; index < archive.records().size(); ++index) {
    const auto& entry = archive.records().at(index);
    if (entry.type == "USER" && entry.publication_name &&
        *entry.publication_name == input.name) {
      if (record) throw std::invalid_argument("Duplicate environment input");
      record = index;
    }
  }
  if (!record) throw std::invalid_argument("Missing environment input bytes");
  return archive.payload(*record);
}
}  // namespace
std::optional<scene::Environment> prepare_stage_environment(
    const content::StageDefinition& stage,
    const std::shared_ptr<memory::Domain>& domain, float initial_camera_far) {
  if (!std::isfinite(initial_camera_far) || initial_camera_far <= 0) {
    throw std::invalid_argument("Invalid initial environment camera far");
  }
  const formats::AreaDefinition* area = nullptr;
  const formats::SectionTable* table = nullptr;
  const content::StageInput* animation = nullptr;
  for (const auto& input : stage.inputs) {
    if (input.name.ends_with(".areadata.bin") && input.area) {
      area = &*input.area;
    }
    if (input.name.ends_with(".areadata.settbl") && input.table) {
      table = &*input.table;
    }
    if (input.name.ends_with(".areadata.filani") && input.scalar_animation) {
      animation = &input;
    }
  }
  if (area == nullptr && table == nullptr && animation == nullptr) return {};
  if (area == nullptr || table == nullptr || animation == nullptr ||
      !stage.environment || table->sections().empty()) {
    throw std::invalid_argument("Incomplete stage environment publications");
  }
  animation::ScalarAnimation curve(input_bytes(stage, *animation), domain);
  scene::EnvironmentChannels initial;
  initial.values.at(kCameraFar) = initial_camera_far;
  initial.smooth.at(kCameraFar) = true;
  auto authored = *stage.environment;
  fields(authored, curve.version(),
         [&](std::size_t channel, const auto& value) {
           initial.values.at(channel) = static_cast<float>(value);
           initial.smooth.at(channel) =
               std::is_floating_point_v<std::remove_cvref_t<decltype(value)>>;
         });
  memory::Vector<scene::EnvironmentRegionBinding> bindings{
      memory::Allocator<scene::EnvironmentRegionBinding>(domain)};
  const auto& section = table->sections().front();
  bindings.reserve(section.rows);
  for (std::size_t row = 0; row < section.rows; ++row) {
    const auto region = table->integer({0, row, 0});
    const auto key = table->integer({0, row, 1});
    if (!region || !key) {
      throw std::invalid_argument("Missing environment table binding");
    }
    bindings.push_back({*region, static_cast<float>(*key)});
  }
  // These source constants blend once per world update, independently of the
  // scalar track's table coordinate. No wall clock or extra tick is created.
  constexpr scene::EnvironmentTransition kTransition{1.0F / 32.0F,
                                                     31.0F / 32.0F, .0001F};
  return scene::Environment(scene::AreaRegions(*area, domain), std::move(curve),
                            bindings, initial, kTransition, domain);
}
formats::StageEnvironment stage_environment_snapshot(
    const formats::StageEnvironment& authored,
    const scene::Environment& environment) {
  auto result = authored;
  fields(result, environment.version(), [&](std::size_t channel, auto& field) {
    using Value = std::remove_reference_t<decltype(field)>;
    const float value = environment.values().at(channel);
    if constexpr (std::is_same_v<Value, bool>) {
      field = value > kBooleanThreshold;
    } else if constexpr (std::is_integral_v<Value>) {
      if (static_cast<double>(value) < std::numeric_limits<Value>::lowest() ||
          static_cast<double>(value) > std::numeric_limits<Value>::max()) {
        throw std::invalid_argument("Environment integer channel overflow");
      }
      field = static_cast<Value>(value);
    } else {
      field = value;
    }
  });
  return result;
}
float stage_environment_camera_far(const scene::Environment& environment) {
  return environment.values().at(kCameraFar);
}
}  // namespace bw::game
