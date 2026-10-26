// Shared math retains each caller's scalar/accumulator precision; tolerance
// and failure policy remain here. Both-phase I.9/MEM.9 contracts are in
// bw/math/vector.hpp and matrix.hpp.
// Fit the shadow warp with double intermediates and validated float rows;
// receiver publication separately retains the source CPU's float rounding.
// The recovered runtime-initialized perspective warp uses the same homogeneous
// transform for casters and receivers. See shadow-projection-baseline.md. The
// jitter path preserves source cell/random traversal and shader channel order.
// Both MCP phases: I.1 (#ri-explicit) exposes all policy inputs; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps fitting on fixed
// arrays and generates the retained volume only during resource setup.
#include "bw/render/shadows/shadow.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "bw/math/vector.hpp"

namespace bw::render {
namespace {
using Vector = math::Vector3<double>;
Vector normalized(Vector value) {
  const double length = math::length(value);
  if (!std::isfinite(length) || length == 0) {
    throw std::invalid_argument("Shadow direction must be finite and nonzero");
  }
  return math::divide(value, length);
}
float random_unit(std::uint32_t& state) {
  constexpr std::uint32_t kMultiplier = 214013;
  constexpr std::uint32_t kIncrement = 2531011;
  constexpr std::uint32_t kMask = 32767;
  constexpr unsigned kOutputShift = 16;
  state = state * kMultiplier + kIncrement;
  return static_cast<float>((state >> kOutputShift) & kMask) /
         static_cast<float>(kMask);
}
std::uint8_t encoded_component(float value) {
  constexpr float kByteMaximum = 255;
  constexpr float kUnitScale = 0.5F;
  // Original CVTTSS2SI emits INT_MIN for NaN; its stored low byte is zero.
  if (!std::isfinite(value)) return 0;
  return static_cast<std::uint8_t>(
      static_cast<int>((value + 1) * kUnitScale * kByteMaximum));
}
}  // namespace

std::size_t shadow_guard_texels(float radius_uv) {
  const double footprint = static_cast<double>(radius_uv) *
                               static_cast<double>(kDirectionalShadowExtent) +
                           0.5;
  if (!std::isfinite(radius_uv) || radius_uv < 0 ||
      std::ceil(footprint) >=
          static_cast<double>(kDirectionalShadowExtent) / 2) {
    throw std::invalid_argument("Shadow filter footprint exceeds map interior");
  }
  return static_cast<std::size_t>(std::ceil(footprint));
}

ShadowReceiverProjection make_shadow_receiver_projection(
    const ShadowProjection& projection,
    const std::array<float, 3>& camera_world_origin) {
#pragma STDC FP_CONTRACT OFF
  // Translate the world projection for camera-relative pixel inputs. Separate
  // multiply/add rounding matches the original SSE matrix publication; fused
  // operations change cancellation near the shadow depth comparison boundary.
  const auto finite = [](float value) { return std::isfinite(value); };
  if (!std::ranges::all_of(camera_world_origin, finite)) {
    throw std::invalid_argument("Shadow receiver camera origin must be finite");
  }
  ShadowReceiverProjection result{projection.clip_rows, {}};
  for (auto& row : result.screen_rows) {
    if (!std::ranges::all_of(row, finite)) {
      throw std::invalid_argument("Shadow receiver projection must be finite");
    }
    row[3] =
        ((camera_world_origin[0] * row[0] + camera_world_origin[1] * row[1]) +
         camera_world_origin[2] * row[2]) +
        row[3];
    if (!std::isfinite(row[3])) {
      throw std::invalid_argument(
          "Shadow receiver translation exceeds float range");
    }
  }
  // Publish the separately remapped texture matrix, retaining depth and W.
  // Receivers apply these rows after world-position interpolation, not before.
  constexpr float kClipToTextureScale = .5F;
  result.texture_rows = result.screen_rows;
  for (std::size_t column = 0; column < 4; ++column) {
    result.texture_rows[0].at(column) =
        kClipToTextureScale * result.screen_rows[0].at(column) +
        kClipToTextureScale * result.screen_rows[3].at(column);
    result.texture_rows[1].at(column) =
        -kClipToTextureScale * result.screen_rows[1].at(column) +
        kClipToTextureScale * result.screen_rows[3].at(column);
  }
  for (const auto& row : result.texture_rows) {
    if (!std::ranges::all_of(row, finite)) {
      throw std::invalid_argument(
          "Shadow receiver texture projection exceeds float range");
    }
  }
  return result;
}

ShadowProjection make_directional_shadow_projection(
    const ViewPose& camera, const PerspectiveView& perspective,
    const ShadowWarpSettings& settings,
    const std::array<float, 3>& light_travel_direction) {
  // Reuse the camera contract's orthonormal/frustum validation. The shadow far
  // plane is stage-authored and may be shorter than the camera's visible range.
  static_cast<void>(make_view_transforms(camera, perspective));
  const std::array controls{settings.far_distance,
                            settings.minimum_height,
                            settings.maximum_height,
                            settings.alignment_threshold,
                            settings.parallel_near_extension,
                            settings.perspective_angle_radians,
                            settings.depth_scale};
  // The original compares against the raw finite alignment control. AREA
  // deliberately supplies values above one to keep the warped branch active.
  // This is a branch threshold, not a normalized cosine input.
  constexpr double kHalfPi = 1.5707963267948966;
  if (!std::ranges::all_of(controls,
                           [](float v) { return std::isfinite(v); }) ||
      settings.far_distance <= perspective.near_plane ||
      settings.far_distance > perspective.far_plane ||
      settings.minimum_height >= settings.maximum_height ||
      settings.parallel_near_extension <= 0 || settings.depth_scale <= 0 ||
      settings.perspective_angle_radians <= 0 ||
      settings.perspective_angle_radians >= kHalfPi) {
    throw std::invalid_argument("Invalid shadow warp controls");
  }
  const auto guard = shadow_guard_texels(settings.filter_radius_uv);
  const auto vector = [](const std::array<float, 3>& v) -> Vector {
    return {v[0], v[1], v[2]};
  };
  const Vector eye = vector(camera.eye),
               camera_forward = vector(camera.forward);
  const auto camera_right = vector(camera.right), camera_up = vector(camera.up);
  const auto travel = normalized(vector(light_travel_direction));
  constexpr double kSourceLightDistance = 10000;
  Vector light_position{}, to_light{};
  for (std::size_t axis = 0; axis < 3; ++axis) {
    light_position.at(axis) = -travel.at(axis) * kSourceLightDistance;
    to_light.at(axis) = light_position.at(axis) - eye.at(axis);
  }
  to_light = normalized(to_light);
  const bool warped = std::abs(math::dot(to_light, camera_forward)) <=
                      settings.alignment_threshold;
  // Original clipping replaces a corner only when its eye ray crosses the
  // selected height plane; it does not clamp every corner's Y coordinate.
  constexpr std::size_t kFrustumCorners = 8;
  std::array<Vector, kFrustumCorners> corners{};
  const double tangent = std::tan(perspective.vertical_fov_radians / 2.0);
  for (std::size_t corner = 0; corner < corners.size(); ++corner) {
    const double distance =
        corner < 4 ? perspective.near_plane : settings.far_distance;
    const double horizontal =
        (corner % 2 == 0 ? -1 : 1) * distance * tangent * perspective.aspect;
    const double vertical = (corner % 4 < 2 ? -1 : 1) * distance * tangent;
    auto& point = corners.at(corner);
    for (std::size_t axis = 0; axis < 3; ++axis) {
      point.at(axis) = eye.at(axis) + camera_forward.at(axis) * distance +
                       camera_right.at(axis) * horizontal +
                       camera_up.at(axis) * vertical;
    }
    for (const auto height :
         {settings.minimum_height, settings.maximum_height}) {
      const bool crosses = height == settings.minimum_height
                               ? eye[1] > height && point[1] < height
                               : eye[1] < height && point[1] > height;
      if (crosses) {
        const double fraction = (height - point[1]) / (eye[1] - point[1]);
        for (std::size_t axis = 0; axis < 3; ++axis) {
          point.at(axis) += (eye.at(axis) - point.at(axis)) * fraction;
        }
      }
    }
  }
  // The ordinary branch swaps light up/forward to perform the perspective
  // warp. Near alignment uses the original alternate light-facing basis.
  Vector forward = to_light;
  if (!warped) {
    for (auto& component : forward) component = -component;
  }
  Vector up_reference = warped ? camera_forward : Vector{0, 1, 0};
  auto right = math::cross(up_reference, forward);
  if (math::dot(right, right) == 0) {
    right = math::cross(Vector{0, 0, -1}, forward);
  }
  right = normalized(right);
  const auto up = normalized(math::cross(forward, right));
  std::array basis{right, warped ? forward : up, warped ? up : forward};
  Vector origin = light_position;
  if (warped) {
    Vector center{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      center.at(axis) = eye.at(axis) * 4;
      for (std::size_t corner = 4; corner < corners.size(); ++corner) {
        center.at(axis) += corners.at(corner).at(axis);
      }
      center.at(axis) =
          center.at(axis) / static_cast<double>(kFrustumCorners) - eye.at(axis);
    }
    const double distance = math::dot(camera_forward, center);
    for (std::size_t axis = 0; axis < 3; ++axis) {
      origin.at(axis) = eye.at(axis) + camera_forward.at(axis) * distance;
    }
  }
  const double cone_tangent = std::tan(settings.perspective_angle_radians);
  double offset = perspective.near_plane;
  double minimum_z = std::numeric_limits<double>::infinity();
  double maximum_z = -minimum_z;
  for (auto& corner : corners) {
    Vector relative{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      relative.at(axis) = corner.at(axis) - origin.at(axis);
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
      corner.at(axis) = math::dot(relative, basis.at(axis));
    }
    const double cone_distance =
        std::max(std::abs(corner[0]), std::abs(corner[1])) / cone_tangent;
    const double minimum_distance =
        warped ? 1 : settings.parallel_near_extension;
    offset =
        std::min(offset, corner[2] - std::max(minimum_distance, cone_distance));
    minimum_z = std::min(minimum_z, corner[2]);
    maximum_z = std::max(maximum_z, corner[2]);
  }
  double near_plane = minimum_z - offset;
  double far_plane = maximum_z - offset;
  if (!warped) {
    near_plane = std::max(near_plane - settings.parallel_near_extension, 1.0);
    far_plane *= settings.depth_scale;
  }
  for (std::size_t axis = 0; axis < 3; ++axis) {
    origin.at(axis) += basis[2].at(axis) * offset;
  }
  std::array<double, 2> minimum{}, maximum{};
  minimum.fill(std::numeric_limits<double>::infinity());
  maximum.fill(-std::numeric_limits<double>::infinity());
  for (const auto& corner : corners) {
    const double distance = corner[2] - offset;
    for (std::size_t axis = 0; axis < 2; ++axis) {
      const double projected = near_plane * corner.at(axis) / distance;
      minimum.at(axis) = std::min(minimum.at(axis), projected);
      maximum.at(axis) = std::max(maximum.at(axis), projected);
    }
  }
  if (!(near_plane > 0 && far_plane > near_plane && maximum[0] > minimum[0] &&
        maximum[1] > minimum[1])) {
    throw std::invalid_argument("Degenerate shadow fit");
  }
  std::array<std::array<double, 4>, 4> rows{};
  for (std::size_t axis = 0; axis < 2; ++axis) {
    const double scale = 2 * near_plane / (maximum.at(axis) - minimum.at(axis));
    const double translation = -(maximum.at(axis) + minimum.at(axis)) /
                               (maximum.at(axis) - minimum.at(axis));
    for (std::size_t component = 0; component < 3; ++component) {
      rows.at(axis).at(component) = scale * basis.at(axis).at(component) +
                                    translation * basis[2].at(component);
      rows.at(axis)[3] -= rows.at(axis).at(component) * origin.at(component);
    }
  }
  const double depth_scale = near_plane / (near_plane - far_plane);
  for (std::size_t component = 0; component < 3; ++component) {
    rows[2].at(component) = depth_scale * basis[2].at(component);
    rows[2][3] -= rows[2].at(component) * origin.at(component);
    rows[3].at(component) = basis[2].at(component);
  }
  rows[2][3] -= depth_scale * far_plane;
  rows[3][3] = -math::dot(basis[2], origin);
  if (warped) {
    // Algebraic composition of the original inverse axis-swap/translation and
    // final depth remap. Its depth offset cancels; retain the authored scale.
    const double projected_height =
        (2 * near_plane * settings.parallel_near_extension -
         (maximum[1] + minimum[1]) * near_plane) /
        (maximum[1] - minimum[1]);
    const double denominator = (settings.depth_scale + 1) * projected_height;
    if (denominator == 0 || !std::isfinite(denominator)) {
      throw std::invalid_argument("Singular shadow depth warp");
    }
    for (std::size_t component = 0; component < 4; ++component) {
      const double unwarped_y = rows[1].at(component);
      rows[0].at(component) = -rows[0].at(component);
      rows[1].at(component) = rows[3].at(component) - 2 * rows[2].at(component);
      rows[2].at(component) =
          unwarped_y / denominator + settings.depth_scale /
                                         (settings.depth_scale + 1) *
                                         rows[3].at(component);
    }
  }
  // Reserve exactly the native cleared/scissored guard ring. This homogeneous
  // scale changes neither the source perspective divide nor reverse depth.
  const double interior = 1 - 2 * static_cast<double>(guard) /
                                  static_cast<double>(kDirectionalShadowExtent);
  ShadowProjection result;
  for (std::size_t row = 0; row < 4; ++row) {
    for (std::size_t column = 0; column < 4; ++column) {
      const auto value = static_cast<float>(rows.at(row).at(column) *
                                            (row < 2 ? interior : 1));
      if (!std::isfinite(value)) {
        throw std::invalid_argument("Shadow projection exceeds float range");
      }
      result.clip_rows.at(row).at(column) = value;
    }
  }
  return result;
}

void make_shadow_jitter(std::span<std::uint8_t, kShadowJitterBytes> rgba,
                        std::uint32_t seed) {
  constexpr float kPi = 3.141592502593994F;
  constexpr float kTau = 6.283185005187988F;
  constexpr float kGrid = 8;
  // Preserve X, Y, then Z traversal: the original random stream is consumed
  // per screen cell, not in the volume's linear storage order.
  for (std::size_t x = 0; x < kShadowJitterExtent; ++x) {
    for (std::size_t y = 0; y < kShadowJitterExtent; ++y) {
      const float rotation = (random_unit(seed) - 1) * 2 * kPi;
      for (std::size_t z = 0; z < kShadowJitterExtent; ++z) {
        const float cell = static_cast<float>((z % 4) * 2);
        const std::size_t radial_cell = 7 - z / 4;
        const float radius = (static_cast<float>(radial_cell) + 0.5F) / kGrid;
        const float angle_one =
            (cell + 0.5F) / kGrid + (random_unit(seed) * 2 - 1) / kGrid;
        const float radius_one = radius + (random_unit(seed) * 2 - 1) / kGrid;
        const float angle_two =
            (cell + 1.5F) / kGrid + (random_unit(seed) * 2 - 1) / kGrid;
        const float radius_two = radius + (random_unit(seed) * 2 - 1) / kGrid;
        const float polar_one = angle_one * kTau + rotation;
        const float polar_two = angle_two * kTau + rotation;
        const float magnitude_one = std::sqrt(radius_one);
        const float magnitude_two = std::sqrt(radius_two);
        const std::size_t offset =
            ((z * kShadowJitterExtent + y) * kShadowJitterExtent + x) * 4;
        // Original byte order stores first.xy then second.xy in BGRA memory.
        // An RGBA texture must swap R/B to reproduce shader-visible samples.
        rgba[offset] = encoded_component(std::cos(polar_two) * magnitude_two);
        rgba[offset + 1] =
            encoded_component(std::sin(polar_one) * magnitude_one);
        rgba[offset + 2] =
            encoded_component(std::cos(polar_one) * magnitude_one);
        rgba[offset + 3] =
            encoded_component(std::sin(polar_two) * magnitude_two);
      }
    }
  }
}
}  // namespace bw::render
