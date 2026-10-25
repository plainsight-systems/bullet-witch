// Select registered shadow owners using the original independent projected-size
// and light-volume contracts. Camera world rows are right/up/forward/eye;
// extents are left/right/bottom/top/near/far. Spheres are already world-space:
// projected and intersection bounds deliberately need not be the same sphere.
// The volume factory validates finite rigid/right-handed cameras, extents and
// direction. Queries require factory-compatible camera/volume values; consumed
// sphere/threshold inputs are validated and throw on invalid values.
// The selection volume is the source 5000-near fit, independent of the final
// warped raster projection. No height clipping or receiver guard is applied.
// Explicit owner flags retain ready-before-forced-before-disabled ordering.
// Fixed owned values, no successful-path allocation, shared state or retention.
// Source: lighting-shadow-owner-contract.md. Both MCP phases: I.1
// (#ri-explicit) exposes owner and coordinate policy; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps bounded storage.
#pragma once

#include <cstdint>

#include "bw/math/frustum.hpp"

namespace bw::scene::visibility {
struct ShadowSelectionCamera {
  math::Affine3<float> world{};
  std::array<float, math::kPerspectiveExtentCount> extents{};
};
struct ShadowCasterSphere {
  math::Vector3<float> center{};
  float radius{};
};
struct ShadowModelCaster {
  ShadowCasterSphere projected{};
  ShadowCasterSphere intersection{};
  bool pose_ready{};
  bool force_visible{};
  bool disabled{};
};
struct ShadowPlacedCaster {
  ShadowCasterSphere sphere{};
  float original_radius{};
  std::uint32_t flags{};
};
enum class ShadowCasterAdmission : std::uint8_t {
  accepted,
  forced,
  too_small,
  inactive,
  outside_volume,
};
constexpr bool shadow_caster_admitted(ShadowCasterAdmission result) {
  return result == ShadowCasterAdmission::accepted ||
         result == ShadowCasterAdmission::forced;
}
// Direction points toward the light and already includes the original +.001 Z
// adjustment. Camera extents describe the active view; shadow_far replaces its
// far plane for this fit. Near < shadow_far <= camera far. No
// mutation/retention.
math::PerspectiveFrustum make_shadow_selection_volume(
    const ShadowSelectionCamera& camera, float shadow_far,
    const math::Vector3<float>& direction_toward_light);
// These queries borrow factory-compatible camera/volume values. Threshold is
// finite/nonnegative. Static owners have projected size1 and always intersect.
ShadowCasterAdmission query_static_shadow_caster(float threshold);
ShadowCasterAdmission query_model_shadow_caster(
    const ShadowSelectionCamera& camera,
    const math::PerspectiveFrustum& light_volume, float threshold,
    const ShadowModelCaster& owner);
// Placed owners require flags bit0x1000; mode7/original-radius>10 with a short
// light volume uses the source expanded active-camera query. The active camera
// is explicitly optional because the original owner handles its absence.
ShadowCasterAdmission query_placed_shadow_caster(
    const ShadowSelectionCamera& projected_camera,
    const math::PerspectiveFrustum& light_volume, float threshold,
    const ShadowPlacedCaster& owner,
    const ShadowSelectionCamera* active_camera);
}  // namespace bw::scene::visibility
