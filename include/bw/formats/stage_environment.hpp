// Authored stage lighting, sky and output settings from the original CSTM
// customization hierarchy. Inputs are immutable borrowed bytes and an injected
// metadata domain; output is a small value with no borrowed storage. Missing
// paths, cyclic/out-of-range links, malformed names and nonfinite settings fail
// with resources::Error. Algorithm: validate the hierarchy's sibling/child/name
// offsets, bound each inline record by the next node, resolve witch/stage/name,
// then decode the selected environment fields. Cost O(bytes + nodes log nodes)
// and temporary domain metadata; no resource copies or native pointers. The
// content owner retains the original archive separately. Both MCP phases:
// cpp-guidelines R.2 (#rr-use-ptr) carries bounded spans; cpp-performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// metadata allocation. Source algorithms and field provenance are recorded in
// stage-content-decoding.md; game/stage_environment maps live area overrides.
// Fog decoding retains separate surface/sky controls and shared authored RGB;
// it does not apply enable fallbacks or camera-relative shader conversion.
// Fog design/implementation reread R.2 and MEM.6: bounded record reads return
// owned scalar values, with no additional allocation or borrowed lifetimes.
#pragma once

#include "bw/formats/common.hpp"
#include "bw/memory/allocator.hpp"

namespace bw::formats {
struct StageToneMapping {
  bool enabled{}, bloom_enabled{};
  float exposure_key{}, gamma{}, bloom_exposure_key{}, bloom_threshold{},
      bloom_soft_knee{};
  std::int32_t history_window{};
  float adaptation_slew{}, minimum_luminance{}, maximum_luminance{};
  std::int32_t color_filter_mode{};
  std::array<float, 3> color_filter{};
};
struct StageFog {
  bool distance_enabled{}, height_gate_enabled{}, height_enabled{};
  float distance_start{}, distance_end{}, density{};
  // Distance fog is multiplied by a descending height gate. The second
  // descending height ramp adds an independently weighted contribution.
  std::array<float, 2> height_gate{}, height_range{};
  float height_opacity{};
};
// Original shadow setup/caster gates and authored receiver/raster controls.
// These retain source values; projection fallback and native state are render
// policy. I.1 (#ri-explicit) exposes the distinction; MEM.9 fixed scalars add
// no storage allocation (both shadow design/implementation consultations).
struct StageShadow {
  bool setup_enabled{}, casters_enabled{};
  std::uint32_t cull_selector{};
  float receiver_radius{}, raster_depth_bias{}, raster_slope_bias{};
  // Raw authored fit controls; stage activation supplies the warp defaults.
  float far_distance{}, minimum_height{}, maximum_height{};
  float alignment_threshold{}, parallel_near_extension{};
};
struct StageEnvironment {
  // Uniform ambient RGB supplied to each ordinary static model's harmonic
  // lighting context. Sky supplies its own lighting independently.
  std::array<float, 3> ambient_color{};
  // Each world-pass build overwrites the separate SHLT owners' multipliers from
  // these fields. They are independent of file intensity and static ambient.
  // I.1 (#ri-explicit)/MEM.9: decode once, pass explicit allocation-free
  // values.
  float character_light_intensity{}, enemy_light_intensity{};
  float sky_yaw_degrees{}, sky_height{};
  std::array<float, 4> light_diffuse{}, light_specular{};
  // Authored source vector; conversion to the shader's normalized direction
  // belongs to the render-view composition contract.
  std::array<float, 3> light_vector{};
  StageFog surface_fog, sky_fog;
  std::array<float, 3> fog_color{};
  StageToneMapping tone_mapping;
  StageShadow shadow;
  static StageEnvironment parse(std::span<const std::uint8_t> customization,
                                std::string_view stage,
                                const std::shared_ptr<memory::Domain>& domain);
};
}  // namespace bw::formats
