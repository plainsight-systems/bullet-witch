// Shared classification of recovered original static surface programs.
// Input flags are the effective original selector after explicit pass policy;
// variant preserves the original low two selector bytes. Return unsupported
// for untraced programs: never select a plausible replacement. Requirements
// describe consumed textures/attributes and equations, not scene readiness.
// Algorithm: bounded exact program-family lookup, then semantic feature decode.
// O(1), no allocation, device types or synchronization. Both design and
// implementation consult E.6 (#re-raii) for owner-independent validation and
// GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md) for setup-time
// classification reused by retained native resources. See
// world-renderer-foundation.md. I.1 (#ri-explicit) also makes the runtime
// irradiance feature explicit instead of assuming it from a material record.
#pragma once
#include <cstdint>

namespace bw::render {
// Explicit inspection contribution policy. The restricted mode removes
// authored directional/local lights and their shadow factors from source-traced
// programs, retaining material, vertex and spherical-harmonic illumination.
// It does not reconstruct those passes or choose a different shader.
enum class WorldLightingPolicy : std::uint8_t {
  supplied,
  without_direct_lighting
};
struct WorldMaterialRequirements {
  bool supported{};
  bool normal_map{}, secondary_base{}, environment_map{};
  bool vertex_color{}, vertex_illumination{};
  bool reflection{}, fresnel{}, directional_light{};
  // Original depth coverage: material alpha * base alpha >= 0.5 at raw UV.
  bool alpha_cutout{};
  bool spherical_harmonics{};
};
WorldMaterialRequirements world_material_requirements(
    std::uint32_t effective_flags, std::uint16_t variant,
    WorldLightingPolicy lighting = WorldLightingPolicy::supplied) noexcept;
}  // namespace bw::render
