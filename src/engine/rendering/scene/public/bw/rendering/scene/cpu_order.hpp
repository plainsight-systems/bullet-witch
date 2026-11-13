#ifndef BW_RENDERING_SCENE_CPU_ORDER_HPP
#define BW_RENDERING_SCENE_CPU_ORDER_HPP

#include "bw/rendering/scene/cluster_records.hpp"
#include "bw/rendering/scene/frame_views.hpp"
#include "bw/rendering/scene/instance.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Responsibility: the draws the CPU issues (docs/architecture/scene.md,
// invariants 5, 13 and 15): the instances with no pipeline index, which
// are the blended meshes, the emitters and the decals, tested against the
// main view's frustum, given a detail level and put in the order the
// forward and decal passes draw them. A pure function of the records, the
// level table and the view; no device.
// The blended order is priority above distance above an id, the distance
// measured from the box's centre, larger distance first; there is one
// camera and no authored distance offset. A box is tested against the
// frustum's planes, with no sphere test first, and no instance carries a
// cull distance.
//
// Contract.
//   1. box_outside(planes, center, extent) is true when the box lies wholly
//      on the outer side of at least one plane: for some plane, a c.x + b
//      c.y + c c.z + d + |a| e.x + |b| e.y + |c| e.z < 0. It is the test the
//      culling dispatch applies to an instance's and a cluster's box, and
//      the one this order applies.
//   2. order_cpu_draws(view, records, levels, out) considers every record
//      that is live and whose pipeline is kNone. One whose current world
//      box is box_outside(view.planes) is absent. Each other gets a detail
//      level: select_detail_level over its levels' screen sizes
//      (levels[first_level .. first_level + level_count)) at
//      projected_radius_squared(squared distance from the box's centre to
//      view.eye, the length of the box's extent, view.projection_scale)
//      (detail_level.hpp); an instance with no levels gets 0.
//      Preconditions: a considered record's level range lies within
//      `levels` and its level_count is at most kMaxDetailLevels; a broken
//      one stops the process with a line on stderr.
//   3. out.blended holds the instances of kind Mesh and Emitter, ordered by
//      sort_priority ascending, then by the squared distance of contract 2
//      descending, then by instance index ascending. out.decals holds the
//      instances of kind Decal, ordered by sort_priority ascending, then by
//      instance index ascending. The same records and view give the same
//      two orders.
//   4. The vectors of `out` are cleared and refilled; their storage is
//      kept, so a caller that reuses one CpuOrder allocates only when a
//      frame holds more than any before.
//
// Core guidelines read: I.1, F.20, Con.5. Performance guidelines read:
// MEM.1 (one reused output), CDSA.4 (the pass walks the dense record
// array).
namespace bw::scene {

struct CpuDraw {
  std::uint32_t instance{};
  std::uint32_t detail_level{};
};

struct CpuOrder {
  std::vector<CpuDraw> blended{};
  std::vector<CpuDraw> decals{};
};

[[nodiscard]] bool box_outside(const std::array<Plane, kFrustumPlanes>& planes,
                               const shaders::Float3& center,
                               const shaders::Float3& extent) noexcept;

void order_cpu_draws(const SceneView& view, std::span<const InstanceRecord> records,
                     std::span<const LevelRecord> levels, CpuOrder& out);

} // namespace bw::scene

#endif
