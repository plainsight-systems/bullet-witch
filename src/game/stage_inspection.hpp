// Both-phase P.9 (#rp-waste) samples unique named curves once and distributes
// through prepared slot indices. MEM.9 reserves all metadata at construction.
// Initial scene presentation selects source-traced ordinary meshes, sky and
// placed props, independently of mission/gameplay readiness. Borrow validated
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
// Fresh mesh-group state and authored fog accompany the initial prop poses.
// The authorized bounds-fit shadow projection is explicit reconstruction
// policy; dynamic visibility, destruction and area overrides remain separate.
// Setup traverses draw, collision and selected vertex metadata; device geometry
// and textures are reused. Game camera and renderer projection are separate
// owners. Design/implementation cpp-guidelines E.6 (#re-raii) pairs view
// publication and cleanup; cpp-performance GPU.1
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
// Background design/implementation also read C.32 (#rc-dtor-ptr) and MEM.9:
// mission-owned definitions/handles are borrowed; stable temporary palettes
// exist only through native publication. See stage-scene-presentation.md.
// Material animation retains only mapped curve views and per-draw update
// records, whose keys remain owned by the mission. Setup snapshots command
// bindings and validates keys; advance samples and publishes texture constants
// using the host game delta. O(animated draws * log keys), no frame allocation
// or sampler/geometry rebuild. Both phases: R.2 (#rr-use-ptr) makes borrowing
// bounded; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps
// all update storage in construction-time domain vectors.
#pragma once
#include <functional>
#include <optional>

#include "bw/content/stage.hpp"
#include "bw/formats/background_placement.hpp"
#include "bw/render/material_animation.hpp"
#include "bw/render/view.hpp"
#include "bw/render/world.hpp"
#include "resident_model.hpp"
namespace bw::game {
// Definitions and model handles have identical order. Their mission owner must
// outlive the view; the renderer validates each handle at submission.
struct StageViewSource {
  std::reference_wrapper<const content::StageDefinition> definition;
  std::span<const render::WorldModelHandle> models;
};
// Optional placed-prop snapshot; all borrowed inputs are consumed during view
// construction. This does not execute mission start or advance physics.
struct StageBackgroundSource {
  const formats::BackgroundPlacementDefinition* placements{};
  std::span<const ResidentModelView> models;
};
class StageInspection {
 public:
  StageInspection(
      render::WorldOutput& output, StageViewSource stage, StageViewSource sky,
      const std::shared_ptr<memory::Domain>& domain,
      const std::optional<render::ViewPose>& camera_pose = std::nullopt,
      StageBackgroundSource background = {});
  ~StageInspection();
  // Advance only retained material curves using the existing host game clock.
  // Borrowed keys remain mission-owned; no geometry/sampler publication or
  // allocation occurs. Invalid/nonfinite deltas reject before updating time.
  void advance(double delta_seconds);
  StageInspection(const StageInspection&) = delete;
  StageInspection& operator=(const StageInspection&) = delete;
  StageInspection(StageInspection&&) = delete;
  StageInspection& operator=(StageInspection&&) = delete;
  std::size_t selected_draws() const { return selected_draws_; }
  std::size_t omitted_draws() const { return omitted_draws_; }
  std::size_t sky_draws() const { return sky_draws_; }
  std::size_t background_draws() const { return background_draws_; }

 private:
  render::WorldOutput& output_;
  struct AnimatedMaterial {
    std::array<std::optional<std::size_t>, 2> curves;
  };
  struct AnimatedCurve {
    std::string_view name;
    render::MaterialFloat2Curve curve;
    std::array<float, 2> value;
  };
  memory::Vector<AnimatedCurve> animated_curves_;
  memory::Vector<AnimatedMaterial> animated_materials_;
  memory::Vector<render::WorldTextureOffsetUpdate> texture_updates_;
  double animation_seconds_{};
  std::size_t selected_draws_{}, omitted_draws_{}, sky_draws_{},
      background_draws_{};
};
}  // namespace bw::game
