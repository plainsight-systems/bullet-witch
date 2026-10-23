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
// stage-content-decoding.md; animated area overrides remain a later consumer.
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
};
struct StageEnvironment {
  // Uniform ambient RGB supplied to each ordinary static model's harmonic
  // lighting context. Sky supplies its own lighting independently.
  std::array<float, 3> ambient_color{};
  float sky_yaw_degrees{}, sky_height{};
  std::array<float, 4> light_diffuse{}, light_specular{};
  // Authored source vector; conversion to the shader's normalized direction
  // belongs to the render-view composition contract.
  std::array<float, 3> light_vector{};
  StageToneMapping tone_mapping;
  static StageEnvironment parse(std::span<const std::uint8_t> customization,
                                std::string_view stage,
                                const std::shared_ptr<memory::Domain>& domain);
};
}  // namespace bw::formats
