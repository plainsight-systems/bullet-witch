#ifndef BW_RENDERING_SCENE_CULLING_HPP
#define BW_RENDERING_SCENE_CULLING_HPP

#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/scene/depth_hierarchy.hpp"
#include "bw/rendering/scene/draw_records.hpp"
#include "bw/rendering/scene/frame_views.hpp"
#include "bw/rendering/scene/instance_set.hpp"
#include "bw/rendering/scene/scene_geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

// Responsibility: the culling dispatch (docs/architecture/scene.md,
// "Visibility", invariants 5 to 7, 9 to 11, 13 and 14): compute work
// declared into a frame graph that tests every live instance with a
// pipeline index against every view of the frame, then every cluster of
// the survivors, and writes each view's draw records and counts. Nothing
// here runs per instance on the CPU and nothing is read back.
// The tests run instance then cluster, in two phases: phase one tests the
// frustum and then last frame's hierarchy with the previous transform and
// view; phase two retests what phase one culled against the refreshed
// hierarchy and draws it the same frame. Every view is tested in one
// dispatch, and order is kept with no atomic append. Shadow views cull by
// frustum only. There is no per-triangle or backface stage: the unit drawn
// is a cluster. A box wholly beyond the tested view's far plane is tested
// against the hierarchy with its nearest depth held at 1; what phase one
// hides so, phase two tests again against the frame's own depth.
//
// Contract.
//   1. declare_culling_phase_one(culling, graph, inputs, frame) declares
//      phase one and gives the frame's record and count buffers, the
//      record buffer's read index and their layout, make_draw_list_layout(view count,
//      InstanceSet::cluster_capacity per pipeline). After it runs, list
//      (view, pipeline, PhaseOne) holds one DrawRecord for every cluster
//      that passes, and its count their number; the records' fields are
//      draw_records.hpp's.
//   2. What passes, for a view. An instance is tested when it is live and
//      has a pipeline index. It fails when its current world box is
//      box_outside the view's planes (cpu_order.hpp). A surviving
//      instance's clusters are those of the detail levels chosen for it
//      (contract 5); a cluster's box is its cooked box under the
//      instance's transform, grown by the instance's max_displacement, and
//      it fails when that box is box_outside the planes.
//   3. Occlusion, main view only, phase one, and only when `inputs.history`
//      is usable (the hierarchy has a history and the views have a
//      previous main view): the instance's previous world box, and a
//      cluster's box under the previous transform, are projected with the
//      previous main view. A box with a corner at or behind the near plane
//      is not tested and passes. Otherwise its screen rectangle is the
//      pixels whose centres the projected corners' rectangle covers,
//      clamped to the view and at least one pixel, pixel row 0 the top
//      of the screen as in the depth target; in hierarchy texels,
//      each pixel coordinate halved; the level read is the lowest at which
//      the rectangle spans at most four texels a side; and the box fails
//      when its nearest depth, lowered by one step of a 16-bit float, is
//      greater than the farthest depth in every texel of the rectangle at
//      that level. Without a usable history phase one tests frustums
//      alone. Every other view tests frustums alone in every frame.
//   4. declare_culling_phase_two(culling, graph, frame, first_build)
//      declares phase two: every instance and cluster of the main view
//      that phase one failed by occlusion is tested again with its current
//      box, the current main view and the hierarchy `first_build`, by the
//      rule of contract 3, and those that pass are written to list
//      (kMainView, pipeline, PhaseTwo). A cluster is in at most one of the
//      main view's two lists. Every cluster in the frustum that the
//      frame's own depth does not hide is in one of them; a cluster phase
//      one passed stays in its list whether or not this frame's depth
//      hides it.
//   5. Detail levels. For an instance with more than one level, the main
//      view's two detail origins each choose a level by
//      select_detail_level over projected_radius_squared of the world
//      box's centre and extent length, and detail_transition gives the one
//      or two levels drawn and their weights, written into each record
//      (detail_level.hpp). Every view draws the levels the main view
//      chose. An instance with one level draws it with weight 0 and no
//      comparison.
//   6. Order. A list's records are in instance index order and, within an
//      instance, in level then cluster table order; record i carries
//      draw_index i. The same instances, geometry and views give the same
//      lists, byte for byte. No list can overflow: its capacity is every
//      cluster that could reach it.
//   7. create_culling reads the dispatch's compute artifacts from
//      `shader_directory` and makes its pipelines. A frame whose largest
//      list capacity exceeds Capabilities::max_multi_draw_count is
//      ListTooLarge at declare_culling_phase_one, with nothing declared.
//      Every failure is returned by name with a detail text.
//   Preconditions of phase one: declare_instance_upload and
//   upload_geometry succeeded this frame, and each pass whose kernel reads
//   the instance records declares the frame's handle of them Sampled
//   (instance_set.hpp, contract 7); everything `inputs` names outlives the frame's
//   execution (the struct itself is read during the call only).
//
// The number of passes, the group sizes and the form of the compaction are
// not part of the contract.
//
// Core guidelines read: I.1, E.27, C.21, R.3 (the inputs are observers).
// Performance guidelines read: GDSA.10 (scan compaction, the count stays
// on the device, the order is declared stable), GPU.1, GPU.6 (every view
// in one dispatch), GDSA.2 (the tests are comparisons; no reduction whose
// order could change a result).
namespace bw::scene {

// A DrawRecord leads with the RHI's record of a counted multi-draw.
static_assert(offsetof(DrawRecord, draw_index) == offsetof(rhi::IndirectDrawRecord, draw_index));
static_assert(offsetof(DrawRecord, index_count) == offsetof(rhi::IndirectDrawRecord, arguments));
static_assert(offsetof(DrawRecord, first_instance) + sizeof(std::uint32_t) ==
              rhi::kIndirectDrawRecordBytes);

enum class CullingError : std::uint8_t {
  None,
  ArtifactFailed,
  PipelineFailed,
  BufferFailed,
  ListTooLarge,
};

struct CullingFailure {
  CullingError error{CullingError::None};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == CullingError::None; }
};

// What a frame's dispatch reads. Observers; the owner keeps each alive
// through the frame's execution.
struct CullingInputs {
  const FrameViews* views{};
  const InstanceSet* instances{};
  const InstanceBuffer* instance_buffer{};
  const SceneGeometry* geometry{};
  const GeometryBuffers* geometry_buffers{};
  const DepthHierarchy* hierarchy{};
  // The frame's import of the hierarchy's history (depth_hierarchy.hpp);
  // read by phase one when usable (contract 3).
  graph::TextureHandle history{};
};

// What a frame's dispatch gives the passes that draw. records_index is
// the record buffer's index in the read buffer array for this frame: what
// a pass writes into RecordDraw::records (draw_records.hpp, contract 2),
// so a pass that draws records needs no view of its own.
struct CullingFrame {
  graph::BufferHandle records{};
  graph::BufferHandle counts{};
  shaders::BufferIndex records_index{};
  DrawListLayout layout{};
};

class Culling {
public:
  Culling();
  Culling(const Culling&) = delete;
  Culling& operator=(const Culling&) = delete;
  Culling(Culling&&) noexcept;
  Culling& operator=(Culling&&) noexcept;
  ~Culling();

private:
  friend struct CullingAccess;
  struct State;
  std::unique_ptr<State> state_;
};

[[nodiscard]] CullingFailure
create_culling(rhi::Device& device, const std::filesystem::path& shader_directory, Culling& out);

[[nodiscard]] CullingFailure declare_culling_phase_one(Culling& culling, rhi::Device& device,
                                                       graph::FrameGraph& graph,
                                                       const CullingInputs& inputs,
                                                       CullingFrame& frame);

void declare_culling_phase_two(Culling& culling, graph::FrameGraph& graph,
                               const CullingFrame& frame, graph::TextureHandle first_build);

} // namespace bw::scene

#endif
