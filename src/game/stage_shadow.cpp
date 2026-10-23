// Compose effective source shadow fit settings when a stage is activated.
// Raw authored fields remain immutable; the runtime initializer supplies the
// recovered warp angle and depth multiplier before observer publication.
// Inputs are borrowed for the call, output is a fixed owned value. No dynamic
// storage, shared state or geometry traversal. Both phases: I.1 (#ri-explicit)
// exposes authored versus runtime policy; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps initialization scalar-only.
#include "stage_shadow.hpp"

#include <numbers>

namespace bw::game {
render::ShadowWarpSettings initial_stage_shadow_settings(
    const formats::StageShadow& authored) {
  // First activation supplies 60 degrees and depth scale 2. The raw archive's
  // zeros are not the values passed to the source projection algorithm.
  constexpr float kRuntimePerspectiveDegrees = 60;
  constexpr float kRuntimeDepthScale = 2;
  constexpr float kDegreesPerHalfTurn = 180;
  return {
      .far_distance = authored.far_distance,
      .minimum_height = authored.minimum_height,
      .maximum_height = authored.maximum_height,
      .alignment_threshold = authored.alignment_threshold,
      .parallel_near_extension = authored.parallel_near_extension,
      .perspective_angle_radians = kRuntimePerspectiveDegrees *
                                   std::numbers::pi_v<float> /
                                   kDegreesPerHalfTurn,
      .depth_scale = kRuntimeDepthScale,
      .filter_radius_uv = authored.receiver_radius /
                          static_cast<float>(render::kDirectionalShadowExtent)};
}

}  // namespace bw::game
