// What the culling dispatch's kernels share (bw/rendering/scene/culling.hpp,
// bw/rendering/scene/compute_shaders.hpp): the implementation's constants,
// the scratch buffers' word layouts, the frustum and hierarchy tests and the
// detail level choice. A kernel includes bw/bindless.hlsli and its generated
// include first; this file reads CullingDispatch and the loaders of the
// blocks that kernel declares, so each function is used only by kernels
// that declare what it reads.
//
// Constants mirrored from culling.cpp (the implementation's, not promised):
//   BW_CULL_GROUP           threads per group of every kernel but the scan,
//                           and the alignment of each (view, pipeline)
//                           segment's first work item.
//   BW_CULL_MAX_GROUPS_X    groups in x of an indirect dispatch; more
//                           continue in y.
// Scratch buffers, raw 32-bit words:
//   candidates       per (view, instance slot), 16 bytes: word 0 bit 0
//                    passes, bit 1 occluded, bit 2 two levels, bits 8 to 11
//                    the lower level, 12 to 15 the higher; word 1 and 2 the
//                    two weights' bits; word 3 the work items it brings.
//   instance groups  per (view, group of BW_CULL_GROUP slots, pipeline), 8
//                    bytes: survivors and work items, then their exclusive
//                    prefixes in the (view, pipeline) segment.
//   segments         per segment view * pipeline_count + pipeline, 16
//                    bytes: first survivor, survivors, first work item,
//                    work items; then 16 bytes of totals: survivors, the
//                    end of the work item space, the end of the main
//                    view's work items, 0.
//   cluster state    per work item, 4 bytes: bit 0 passes this phase, bit
//                    1 phase one rejected it by occlusion.
//   cluster groups   per group of BW_CULL_GROUP work items, 4 bytes: its
//                    passing count, then its exclusive prefix in its
//                    segment.
//   arguments        two dispatch argument triples, 24 bytes: every work
//                    group, then the main view's.
#ifndef BW_SCENE_CULLING_HLSLI
#define BW_SCENE_CULLING_HLSLI

#include "bw/scene/half_depth.hlsli"
#include "bw/scene/records.hlsli"

#define BW_CULL_GROUP 64u
#define BW_CULL_MAX_GROUPS_X 65535u
#define BW_CANDIDATE_BYTES 16u
#define BW_INSTANCE_GROUP_BYTES 8u
#define BW_SEGMENT_BYTES 16u
#define BW_CULLING_VIEW_BYTES 208u
#define BW_CULLING_LIST_BYTES 16u
#define BW_CULL_SURVIVOR_BYTES 48u
#define BW_CLUSTER_RECORD_BYTES 48u
#define BW_LEVEL_RECORD_BYTES 16u
#define BW_NONE 0xFFFFFFFFu
// instance.hpp, kInstanceLive.
#define BW_INSTANCE_LIVE (1u << 10)
// compute_shaders.hpp, kCullingHistoryUsable and kSurvivorOccluded.
#define BW_CULLING_HISTORY_USABLE 1u
#define BW_SURVIVOR_OCCLUDED 1u

#define BW_CANDIDATE_PASS 1u
#define BW_CANDIDATE_OCCLUDED 2u
#define BW_CANDIDATE_TWO_LEVELS 4u
#define BW_CLUSTER_PASS 1u
#define BW_CLUSTER_REJECTED 2u

uint bw_instance_groups_per_view() {
  return (CullingDispatch.instance_capacity + BW_CULL_GROUP - 1u) / BW_CULL_GROUP;
}

uint bw_segment_count() { return CullingDispatch.view_count * CullingDispatch.pipeline_count; }

// The groups in x an indirect dispatch over `groups` groups runs with.
uint bw_groups_x(uint groups) { return min(groups, BW_CULL_MAX_GROUPS_X); }

// cpu_order.hpp, box_outside: outside when the box is wholly behind any
// one plane.
bool bw_box_outside(float4 planes[6], float3 center, float3 extent) {
  for (uint i = 0; i < 6u; ++i) {
    const float4 p = planes[i];
    const float distance = p.x * center.x + p.y * center.y + p.z * center.z + p.w;
    const float reach = abs(p.x) * extent.x + abs(p.y) * extent.y + abs(p.z) * extent.z;
    if (distance + reach < 0.0) {
      return true;
    }
  }
  return false;
}

// instance.hpp, world_box: the box of the eight corners of (center ±
// extent) under `m` (row vectors), grown by `pad`, as centre and extent.
void bw_world_box(float3 center, float3 extent, float4x4 m, float pad, out float3 out_center,
                  out float3 out_extent) {
  float3 lo = float3(0.0, 0.0, 0.0);
  float3 hi = float3(0.0, 0.0, 0.0);
  for (uint k = 0; k < 8u; ++k) {
    const float3 corner = float3((k & 1u) != 0u ? center.x + extent.x : center.x - extent.x,
                                 (k & 2u) != 0u ? center.y + extent.y : center.y - extent.y,
                                 (k & 4u) != 0u ? center.z + extent.z : center.z - extent.z);
    const float3 p = corner.x * m[0].xyz + corner.y * m[1].xyz + corner.z * m[2].xyz + m[3].xyz;
    lo = k == 0u ? p : min(lo, p);
    hi = k == 0u ? p : max(hi, p);
  }
  lo -= float3(pad, pad, pad);
  hi += float3(pad, pad, pad);
  out_center = (lo + hi) * 0.5;
  out_extent = (hi - lo) * 0.5;
}

// culling.hpp, contract 3: whether the box (world centre and extent)
// passes the hierarchy `hierarchy` under the view `world_to_clip` of
// `viewport` pixels. A box with a corner at or behind the near plane
// passes untested. The rectangle is the pixels whose centres the corners'
// rectangle covers, clamped to the view and at least one pixel, halved into
// level 0 texels; the level is the lowest at which it spans at most four
// texels a side; the box fails when its nearest depth lowered by one step
// of a 16-bit float (bw/scene/half_depth.hlsli) is above the farthest depth of every texel there.
// The 4 by 4 texels are read one by one, not gathered, which every target
// runs.
bool bw_hierarchy_visible(float3 center, float3 extent, float4x4 world_to_clip, uint hierarchy,
                          uint2 hierarchy_size, uint levels, float2 viewport) {
  float2 lo = float2(0.0, 0.0);
  float2 hi = float2(0.0, 0.0);
  float nearest = 1.0;
  for (uint k = 0; k < 8u; ++k) {
    const float3 corner = float3((k & 1u) != 0u ? center.x + extent.x : center.x - extent.x,
                                 (k & 2u) != 0u ? center.y + extent.y : center.y - extent.y,
                                 (k & 4u) != 0u ? center.z + extent.z : center.z - extent.z);
    const float4 clip = mul(float4(corner, 1.0), world_to_clip);
    if (clip.w <= 0.0 || clip.z <= 0.0) {
      return true;
    }
    const float3 ndc = clip.xyz / clip.w;
    lo = k == 0u ? ndc.xy : min(lo, ndc.xy);
    hi = k == 0u ? ndc.xy : max(hi, ndc.xy);
    nearest = min(nearest, ndc.z);
  }
  // Texture rows run down while clip y runs up.
  const float2 uv_lo = float2(lo.x * 0.5 + 0.5, 0.5 - hi.y * 0.5);
  const float2 uv_hi = float2(hi.x * 0.5 + 0.5, 0.5 - lo.y * 0.5);
  const float2 last = viewport - float2(1.0, 1.0);
  const float2 first_pixel = clamp(ceil(uv_lo * viewport - 0.5), float2(0.0, 0.0), last);
  const float2 last_pixel = max(clamp(floor(uv_hi * viewport - 0.5), float2(0.0, 0.0), last),
                                first_pixel);
  const uint2 t0 = uint2(first_pixel) >> 1;
  const uint2 t1 = uint2(last_pixel) >> 1;
  uint level = 0;
  while (level + 1u < levels &&
         (((t1.x >> level) - (t0.x >> level)) > 3u || ((t1.y >> level) - (t0.y >> level)) > 3u)) {
    ++level;
  }
  const uint2 level_size = max(hierarchy_size >> level, uint2(1u, 1u));
  float farthest = 0.0;
  for (uint y = t0.y >> level; y <= (t1.y >> level); ++y) {
    for (uint x = t0.x >> level; x <= (t1.x >> level); ++x) {
      const int3 texel =
          int3(int(min(x, level_size.x - 1u)), int(min(y, level_size.y - 1u)), int(level));
      farthest = max(farthest, BW_TEXTURE2D(hierarchy).Load(texel).r);
    }
  }
  // The step down: the value of the code below the nearest depth's code,
  // code 0 staying as it is. The hierarchy stores its texels through the
  // same non-decreasing packing, so a box whose nearest depth is not
  // greater than the true farthest depth has a code no greater than the
  // stored one, and the code below it is never greater than it: a visible
  // box is never culled.
  const uint code = bw_depth_to_half(nearest);
  const float lowered = f16tof32(code == 0u ? 0u : code - 1u);
  return !(lowered > farthest);
}

// The totals after the segments: survivors, the end of the work item
// space, the end of the main view's work items.
uint3 bw_segment_totals() {
  return BW_BUFFER(CullingDispatch.segments_read).Load3(bw_segment_count() * BW_SEGMENT_BYTES);
}

// The work item a thread of an indirect dispatch over `items` work items
// takes, from its group and lane (bw_groups_x).
uint bw_work_item(uint3 group, uint lane, uint items) {
  const uint x = bw_groups_x(items / BW_CULL_GROUP);
  return (group.y * x + group.x) * BW_CULL_GROUP + lane;
}

// The survivor whose work items hold `item`: the last survivor whose first
// work item is at or below it, survivors being in work item order. Its
// index, or BW_NONE when no survivor starts at or below it.
uint bw_find_survivor(uint item, uint survivors) {
  uint found = BW_NONE;
  uint low = 0u;
  uint high = survivors;
  while (low < high) {
    const uint middle = (low + high) / 2u;
    const uint first =
        BW_BUFFER(CullingDispatch.survivors_read).Load(middle * BW_CULL_SURVIVOR_BYTES + 8u);
    if (first <= item) {
      found = middle;
      low = middle + 1u;
    } else {
      high = middle;
    }
  }
  return found;
}

#endif
