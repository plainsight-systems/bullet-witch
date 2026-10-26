// Classify source-selected surface programs at resource preparation, before
// native stream/texture admission. Input is the complete effective original
// selector plus texture-layer variant; unknown combinations return unsupported.
// No actor/terrain guesses or nearest-family fallback are permitted.
// Requirements describe consumed inputs and equations, not scene readiness or
// pass ownership. Algorithm: exact audited-key binary search, then original
// feature dependencies. Bounded O(log N) lookup, no allocation, ownership,
// device types or mutable state. Both design and implementation retrieve I.1
// (#ri-explicit): separate shadow, input and coverage requirements instead of
// inferring them from directional light. CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-
// layout.md) classifies once for retained native packets; GPU.4
// (guidelines/gpu/ GPU.4-keep-warp-wave-control-flow-coherent.md) keeps shader
// decisions draw-uniform. Evidence and native limits:
// research/engine/subsystems/lighting-material-redesign.md.
#pragma once
#include <cstdint>

namespace bw::render {
// Explicit inspection-only energy suppression after source classification.
// It cannot change effective selector admission or substitute another program.
enum class WorldLightingPolicy : std::uint8_t {
  supplied,
  without_direct_lighting
};
struct WorldMaterialRequirements {
  bool supported{};
  bool normal_map{}, secondary_base{}, environment_map{};
  bool vertex_color{}, vertex_illumination{};
  bool reflection{}, fresnel{}, directional_light{}, local_lights{};
  // Environment response and direct-light shadow sampling have independent
  // source dependencies. Caster eligibility belongs to the submitting owner.
  bool environment_shadow{}, shadow_reception{};
  // Original masked depth/shadow program: material alpha * raw-UV base alpha
  // must reach 0.5. Final-color alpha testing belongs to the render phase and
  // evaluates the finished shader alpha, including indirect/vertex/Fresnel.
  bool depth_cutout{};
  bool spherical_harmonics{};
  bool base_texture{};
  // Skinned families use the complete selector field: 0/1/2/3 select 4/3/2/1
  // joints. Non-skinned surfaces consume no palette. Validation covers every
  // used index; three influences do not read or normalize a fourth weight.
  std::uint8_t joint_influences{};
  bool ambient_occlusion{};
  bool secondary_alpha_blend{}, alpha_fresnel{};
  // Includes local-only, environment and alpha-Fresnel consumers. Neither
  // directional-light enable nor normal-map presence alone is sufficient.
  bool normal_required{};
};
WorldMaterialRequirements world_material_requirements(
    std::uint32_t effective_flags, std::uint16_t variant,
    WorldLightingPolicy lighting = WorldLightingPolicy::supplied) noexcept;
}  // namespace bw::render
