// Static-world inspection selects a bounded, declared subset of original
// materials, independently of mission/gameplay readiness. Borrow validated
// stage CPU definitions during construction and mission-owned device handles
// through destruction. Own only the submitted view; the mission loads each
// geometry/texture resource once. Exclusive main-thread access. Destroy the
// view before cancelling its mission resources. A failed submission leaves the
// output's prior draw list intact; this object publishes no partial view.
// Algorithm: select all ordinary static world objects and traced materials;
// decode the retained player camera settings; compose the recovered
// fresh-mission player pose through the original initial camera and static
// clearance queries; derive the renderer view and reverse-depth projection;
// center the sky on the camera with authored yaw/height and the source constant
// white spherical-harmonic input; supply authored stage RGB harmonics to
// ordinary objects; publish resident draws
// with authored light/output settings. An optional explicit pose replaces only
// the inspection eye/basis, retaining the authored perspective. It does not
// change the scripted pose, execute actor movement or run collision on that
// diagnostic pose. The shared view validator rejects invalid bases before
// publication. The scripted pose
// is an inspection bootstrap: mission start and player simulation have not run.
// Full visibility, shadows and area overrides remain outside this view. Setup
// traverses draw and collision metadata; device geometry and textures are
// reused. Game camera and renderer projection are separate owners.
// Design/implementation cpp-guidelines E.6 (#re-raii) pairs view publication
// and cleanup; cpp-performance GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md) reuses mission-owned device
// storage. Both rules were retrieved for design and implementation. Temporary
// draw metadata uses the resource domain. See stage-view-completion.md,
// mission-launch.md and world-renderer-state.md;
// camera initialization and reversed-depth view projection follow the original
// consumers recorded in player-camera-initialization.md.
// Both MCP phases for camera/sky composition: I.1 (#ri-explicit) exposes the
// optional diagnostic policy; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) uses fixed transforms
// and immutable packed irradiance coefficients, computed only at view setup.
#pragma once
#include <functional>
#include <optional>

#include "bw/content/stage.hpp"
#include "bw/render/view.hpp"
#include "bw/render/world.hpp"
namespace bw::game {
// Definitions and model handles have identical order. Their mission owner must
// outlive the view; the renderer validates each handle at submission.
struct StageViewSource {
  std::reference_wrapper<const content::StageDefinition> definition;
  std::span<const render::WorldModelHandle> models;
};
class StageInspection {
 public:
  StageInspection(
      render::WorldOutput& output, StageViewSource stage, StageViewSource sky,
      const std::shared_ptr<memory::Domain>& domain,
      const std::optional<render::ViewPose>& camera_pose = std::nullopt);
  ~StageInspection();
  StageInspection(const StageInspection&) = delete;
  StageInspection& operator=(const StageInspection&) = delete;
  StageInspection(StageInspection&&) = delete;
  StageInspection& operator=(StageInspection&&) = delete;
  std::size_t selected_draws() const { return selected_draws_; }
  std::size_t omitted_draws() const { return omitted_draws_; }
  std::size_t sky_draws() const { return sky_draws_; }

 private:
  render::WorldOutput& output_;
  std::size_t selected_draws_{}, omitted_draws_{}, sky_draws_{};
};
}  // namespace bw::game
