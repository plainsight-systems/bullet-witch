// Reconstruct the source camera-dependent shadow perspective warp, shared by
// caster rasterization and receiver depth comparison. Camera inputs follow
// view.hpp; stage fit controls and runtime initialization are explicit values.
// Build eight camera corners, clip crossing eye rays to the authored height
// slab, choose the source alignment branch, fit its perspective cone and
// compose the homogeneous warp. Inset X/Y for the filter guard, preserving
// source depth and W. Invalid, singular or unrepresentable inputs throw before
// publication. Fixed arrays, bounded scalar work, no successful-path allocation
// or shared state. Jitter writes a caller-owned volume with an explicit seed.
// See research/engine/subsystems/shadow-projection-baseline.md. Both phases:
// I.1 (#ri-explicit) exposes coordinates and effective runtime settings; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) uses fixed scratch.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "bw/render/view.hpp"

namespace bw::render {
inline constexpr std::size_t kDirectionalShadowExtent = 1024;
// Reserve nearest-sample footprint plus the largest normalized jitter offset.
// Finite nonnegative radius; rejects guards occupying half the map or more.
std::size_t shadow_guard_texels(float radius_uv);

struct ShadowProjection {
  std::array<std::array<float, 4>, 4> clip_rows{};
};
// Pixel receivers consume camera-relative world positions. Keep their screen
// and texture projections separate: remapping an interpolated clip position
// does not preserve the source CPU/fragment operation order.
struct ShadowReceiverProjection {
  std::array<std::array<float, 4>, 4> screen_rows{}, texture_rows{};
};
// Translate each screen row's constant by the camera origin using successive
// float multiply/add operations, then form texture X=.5X+.5W, Y=-.5Y+.5W.
// Depth/W rows are unchanged. Finite inputs and outputs are required; throws
// invalid_argument before returning any result. No allocation or input mutation
// on success. The caller establishes the camera-relative basis; this helper
// neither rotates nor inverts it. Original CPU witness: receiver_matrix_witness
// under ignored lighting-noise-material evidence (451fa0/406690).
ShadowReceiverProjection make_shadow_receiver_projection(
    const ShadowProjection& projection,
    const std::array<float, 3>& camera_world_origin);
struct ShadowWarpSettings {
  float far_distance{}, minimum_height{}, maximum_height{};
  float alignment_threshold{}, parallel_near_extension{};
  float perspective_angle_radians{}, depth_scale{}, filter_radius_uv{};
};
// Height minimum < maximum, near < shadow far <= camera far; finite alignment
// branch threshold, positive extension/depth scale, angle in (0,pi/2). Light
// travels away from the source. Guard inset is derived from filter_radius_uv.
ShadowProjection make_directional_shadow_projection(
    const ViewPose& camera, const PerspectiveView& perspective,
    const ShadowWarpSettings& settings,
    const std::array<float, 3>& light_travel_direction);

inline constexpr std::size_t kShadowJitterExtent = 32;
inline constexpr std::size_t kShadowJitterBytes =
    kShadowJitterExtent * kShadowJitterExtent * kShadowJitterExtent * 4;
// Output is RGBA for shader sampling, converted from the original BGRA byte
// storage. Z-major slices contain Y rows and X texels. Seed choice is explicit.
void make_shadow_jitter(std::span<std::uint8_t, kShadowJitterBytes> rgba,
                        std::uint32_t seed);
}  // namespace bw::render
