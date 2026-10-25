// Preserve independent original shadow-owner tests and the separate selection
// camera; raster warp ownership stays in render. Both-phase Core I.1
// (#ri-explicit) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keep flags explicit,
// borrowed values immutable and all scratch fixed-size.
#include "bw/scene/visibility/shadow_casters.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace bw::scene::visibility {
namespace {
constexpr std::size_t kFar = 5;
void validate_threshold(float threshold) {
  if (!std::isfinite(threshold) || threshold < 0) {
    throw std::invalid_argument("Invalid shadow caster threshold");
  }
}
void validate_sphere(const ShadowCasterSphere& sphere) {
  if (!math::all_finite(sphere.center) || !std::isfinite(sphere.radius) ||
      sphere.radius < 0) {
    throw std::invalid_argument("Invalid shadow caster sphere");
  }
}
float projected_size(const ShadowSelectionCamera& camera,
                     const ShadowCasterSphere& sphere) {
  validate_sphere(sphere);
  const auto relative = math::subtract(sphere.center, camera.world[3]);
  const float depth = math::dot(relative, camera.world[2]);
  if (depth <= 0) return std::numeric_limits<float>::max();
  const float scale =
      camera.extents[4] * 2 / (camera.extents[3] - camera.extents[2]);
  return sphere.radius * scale / depth;
}
ShadowCasterAdmission intersection(const math::PerspectiveFrustum& volume,
                                   const ShadowCasterSphere& sphere) {
  validate_sphere(sphere);
  // Original accepted scores may be negative; the five-plane predicate alone
  // distinguishes the original negative-max-float rejection sentinel.
  return math::intersects(volume, sphere.center, sphere.radius)
             ? ShadowCasterAdmission::accepted
             : ShadowCasterAdmission::outside_volume;
}
math::Affine3<float> selection_camera(const ShadowSelectionCamera& camera,
                                      const math::Vector3<float>& direction) {
  constexpr float kLightDistance = 10000;
  constexpr float kUpParallelThreshold = .999F;
  const auto forward = math::scale(direction, -1.F);
  auto right = math::cross(math::Vector3<float>{0, 1, 0}, forward);
  if (math::dot(right, right) <= 0 && forward[1] > kUpParallelThreshold) {
    right = math::cross(math::Vector3<float>{0, 0, -1}, forward);
  }
  const float right_length = math::length(right);
  right = right_length < std::numeric_limits<float>::epsilon()
              ? math::Vector3<float>{1, 0, 0}
              : math::scale(right, 1.F / right_length);
  const auto up_cross = math::cross(forward, right);
  const auto up = math::scale(up_cross, 1.F / math::length(up_cross));
  const auto unit_forward = math::scale(forward, 1.F / math::length(forward));
  return {right, up, unit_forward,
          math::subtract(camera.world[3],
                         math::scale(unit_forward, kLightDistance))};
}
}  // namespace

math::PerspectiveFrustum make_shadow_selection_volume(
    const ShadowSelectionCamera& camera, float shadow_far,
    const math::Vector3<float>& direction_toward_light) {
  static_cast<void>(
      math::make_perspective_frustum(camera.world, camera.extents));
  if (!std::isfinite(shadow_far) || shadow_far <= camera.extents[4] ||
      shadow_far > camera.extents[kFar] ||
      !math::all_finite(direction_toward_light) ||
      !(math::length(direction_toward_light) > 0)) {
    throw std::invalid_argument("Invalid shadow selection fit");
  }
  // This is the original unwarped 5000-near owner-selection fit. Its corners
  // come from the active camera without the raster warp's height clipping.
  constexpr float kSelectionNear = 5000;
  constexpr std::size_t kCorners = 8;
  const auto light = selection_camera(camera, direction_toward_light);
  const auto inverse = math::inverse_affine(light);
  if (!inverse) throw std::invalid_argument("Singular shadow selection camera");
  std::array<float, math::kPerspectiveExtentCount> extents{
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::lowest(),
      std::numeric_limits<float>::max(),
      std::numeric_limits<float>::lowest(),
      kSelectionNear,
      std::numeric_limits<float>::lowest()};
  for (std::size_t corner = 0; corner < kCorners; ++corner) {
    const float distance = corner < 4 ? camera.extents[4] : shadow_far;
    const float scale = distance / camera.extents[4];
    const math::Vector3<float> point{
        camera.extents.at(corner % 2) * scale,
        camera.extents.at(2 + (corner % 4) / 2) * scale, distance};
    const auto world = math::transform_point_basis_first(point, camera.world);
    const auto local = math::transform_point_basis_first(world, *inverse);
    if (!(local[2] > 0)) {
      throw std::invalid_argument("Shadow selection corner behind light");
    }
    const float projection = kSelectionNear / local[2];
    extents[0] = std::min(extents[0], local[0] * projection);
    extents[1] = std::max(extents[1], local[0] * projection);
    extents[2] = std::min(extents[2], local[1] * projection);
    extents[3] = std::max(extents[3], local[1] * projection);
    extents[kFar] = std::max(extents[kFar], local[2]);
  }
  return math::make_perspective_frustum(light, extents);
}

ShadowCasterAdmission query_static_shadow_caster(float threshold) {
  validate_threshold(threshold);
  return threshold <= 1 ? ShadowCasterAdmission::accepted
                        : ShadowCasterAdmission::too_small;
}
ShadowCasterAdmission query_model_shadow_caster(
    const ShadowSelectionCamera& camera,
    const math::PerspectiveFrustum& light_volume, float threshold,
    const ShadowModelCaster& owner) {
  validate_threshold(threshold);
  if (!owner.force_visible &&
      projected_size(camera, owner.projected) < threshold) {
    return ShadowCasterAdmission::too_small;
  }
  // Model state precedes bound testing; forced visibility cannot make an
  // unprepared pose ready, but does take precedence over the disable flag.
  if (!owner.pose_ready) return ShadowCasterAdmission::inactive;
  if (owner.force_visible) return ShadowCasterAdmission::forced;
  if (owner.disabled) return ShadowCasterAdmission::inactive;
  return intersection(light_volume, owner.intersection);
}
ShadowCasterAdmission query_placed_shadow_caster(
    const ShadowSelectionCamera& projected_camera,
    const math::PerspectiveFrustum& light_volume, float threshold,
    const ShadowPlacedCaster& owner,
    const ShadowSelectionCamera* active_camera) {
  validate_threshold(threshold);
  if (!std::isfinite(owner.original_radius) || owner.original_radius < 0) {
    throw std::invalid_argument("Invalid placed shadow original radius");
  }
  if (projected_size(projected_camera, owner.sphere) < threshold) {
    return ShadowCasterAdmission::too_small;
  }
  constexpr std::uint32_t kEnabled = 0x1000, kLargeMode = 7;
  if ((owner.flags & kEnabled) == 0) return ShadowCasterAdmission::inactive;
  constexpr float kShortVolume = 2000, kLargeRadius = 10, kMinimumFar = 1000;
  if (active_camera != nullptr && light_volume.depth_range <= kShortVolume &&
      (owner.flags & kLargeMode) == kLargeMode &&
      owner.original_radius > kLargeRadius) {
    // Large placed objects use the expanded active-camera volume only in this
    // source mode. A missing active camera retains the ordinary light query.
    const float far = std::max(active_camera->extents[kFar], kMinimumFar);
    const float distance = math::length(
        math::subtract(active_camera->world[3], owner.sphere.center));
    if (distance - owner.sphere.radius > far) {
      return ShadowCasterAdmission::outside_volume;
    }
    auto extents = active_camera->extents;
    extents[kFar] = far * 2;
    return intersection(
        math::make_perspective_frustum(active_camera->world, extents),
        owner.sphere);
  }
  return intersection(light_volume, owner.sphere);
}
}  // namespace bw::scene::visibility
