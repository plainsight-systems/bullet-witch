#include "bw/rendering/scene/cpu_order.hpp"

#include "bw/platform/math/math.hpp"
#include "bw/rendering/scene/detail_level.hpp"
#include "bw/rendering/scene/frame_views.hpp"
#include "bw/rendering/scene/instance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>

// Responsibility: the out-of-line part of bw/rendering/scene/cpu_order.hpp.
//
// A considered record whose level range leaves `levels`, or holds more than
// kMaxDetailLevels levels, breaks the header's precondition and stops the
// process with a line on stderr.
namespace bw::scene {

// A box is tested against the frustum's planes, with no sphere test first.
bool box_outside(const std::array<Plane, kFrustumPlanes>& planes, const shaders::Float3& center,
                 const shaders::Float3& extent) noexcept {
  for (const Plane& p : planes) {
    const float distance = (p.a * center.x) + (p.b * center.y) + (p.c * center.z) + p.d;
    const float reach =
        (std::fabs(p.a) * extent.x) + (std::fabs(p.b) * extent.y) + (std::fabs(p.c) * extent.z);
    if (distance + reach < 0.0F) {
      return true;
    }
  }
  return false;
}

namespace {

float distance_squared_of(const SceneView& view, const InstanceRecord& record) noexcept {
  const float dx = record.bounds_center.x - view.eye.x;
  const float dy = record.bounds_center.y - view.eye.y;
  const float dz = record.bounds_center.z - view.eye.z;
  return (dx * dx) + (dy * dy) + (dz * dz);
}

std::uint32_t level_of(const SceneView& view, const InstanceRecord& record,
                       std::span<const LevelRecord> levels) noexcept {
  if (record.level_count == 0U) {
    return 0;
  }
  const std::uint64_t end = std::uint64_t{record.first_level} + record.level_count;
  if (end > levels.size() || record.level_count > kMaxDetailLevels) {
    std::fputs("bw::scene: order_cpu_draws was given a record whose level range leaves the "
               "level table or exceeds kMaxDetailLevels (cpu_order.hpp, contract 2)\n",
               stderr);
    std::abort();
  }
  std::array<float, kMaxDetailLevels> sizes{};
  const std::uint32_t count = record.level_count;
  for (std::uint32_t l = 0; l < count; ++l) {
    sizes[l] = levels[record.first_level + l].screen_size;
  }
  const shaders::Float3& e = record.bounds_extent;
  const float radius = platform::math::square_root((e.x * e.x) + (e.y * e.y) + (e.z * e.z));
  return select_detail_level(
      std::span<const float>(sizes.data(), count),
      projected_radius_squared(distance_squared_of(view, record), radius, view.projection_scale));
}

} // namespace

// The blended key is priority above distance above an id; the distance is
// measured from the box's centre, larger distance first.
void order_cpu_draws(const SceneView& view, std::span<const InstanceRecord> records,
                     std::span<const LevelRecord> levels, CpuOrder& out) {
  out.blended.clear();
  out.decals.clear();
  for (std::size_t i = 0; i < records.size(); ++i) {
    const InstanceRecord& record = records[i];
    if ((record.kind_and_flags & kInstanceLive) == 0U || record.pipeline != kNone ||
        box_outside(view.planes, record.bounds_center, record.bounds_extent)) {
      continue;
    }
    const CpuDraw draw{static_cast<std::uint32_t>(i), level_of(view, record, levels)};
    const auto kind = static_cast<InstanceKind>(record.kind_and_flags & kInstanceKindMask);
    (kind == InstanceKind::Decal ? out.decals : out.blended).push_back(draw);
  }
  // Every key ends in the instance index, so the order is total and the
  // sort's own order of equal elements never shows.
  std::sort(out.blended.begin(), out.blended.end(), [&](const CpuDraw& a, const CpuDraw& b) {
    const InstanceRecord& x = records[a.instance];
    const InstanceRecord& y = records[b.instance];
    if (x.sort_priority != y.sort_priority) {
      return x.sort_priority < y.sort_priority;
    }
    const float dx = distance_squared_of(view, x);
    const float dy = distance_squared_of(view, y);
    if (dx != dy) {
      return dx > dy;
    }
    return a.instance < b.instance;
  });
  std::sort(out.decals.begin(), out.decals.end(), [&](const CpuDraw& a, const CpuDraw& b) {
    const std::int32_t pa = records[a.instance].sort_priority;
    const std::int32_t pb = records[b.instance].sort_priority;
    return pa != pb ? pa < pb : a.instance < b.instance;
  });
}

} // namespace bw::scene
