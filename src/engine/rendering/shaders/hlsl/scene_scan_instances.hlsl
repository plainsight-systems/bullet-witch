// The culling dispatch's scan over the instance test's group counts
// (bw/rendering/scene/culling.hpp, contracts 6 and 7; compute_shaders.hpp,
// contract 1): one group, run once in phase one.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// SceneScanInstances): compute, entry main, per-draw block CullingDispatch,
// no buffer block.
//
// Each lane takes segments (view * pipeline_count + pipeline) in turn and
// turns their groups' survivor and work counts into exclusive prefixes in
// place. Lane 0 then places the segments in segment order, each segment's
// first work item a multiple of BW_CULL_GROUP so a group of work items
// never spans two lists, writes the totals and the two indirect dispatches:
// every work group, then the main view's (bw/scene/culling.hlsli). The
// counts are sums in a fixed order; nothing depends on lane timing. The
// cluster stage is dispatched over what the instance stage kept.
#include "bw/bindless.hlsli"
#include "bw/generated/scene_scan_instances.hlsli"
#include "bw/scene/culling.hlsli"

// frame_views.hpp kMaxViews * draw_records.hpp kMaxPipelines.
#define BW_MAX_SEGMENTS 2048u

groupshared uint g_survivors[BW_MAX_SEGMENTS];
groupshared uint g_work[BW_MAX_SEGMENTS];

void bw_write_dispatch(uint offset, uint items) {
  const uint groups = items / BW_CULL_GROUP;
  const uint x = bw_groups_x(groups);
  const uint y = x == 0u ? 1u : (groups + x - 1u) / x;
  BW_STORAGE_BUFFER(CullingDispatch.arguments).Store3(offset, uint3(x, y, 1u));
}

[numthreads(64, 1, 1)] void main(uint lane : SV_GroupIndex) {
  const uint segments = bw_segment_count();
  const uint groups = bw_instance_groups_per_view();
  const uint pipelines = CullingDispatch.pipeline_count;
  for (uint s = lane; s < segments; s += BW_CULL_GROUP) {
    const uint view_index = s / pipelines;
    const uint pipeline = s % pipelines;
    uint survivors = 0u;
    uint work = 0u;
    for (uint g = 0; g < groups; ++g) {
      const uint address =
          ((view_index * groups + g) * pipelines + pipeline) * BW_INSTANCE_GROUP_BYTES;
      const uint2 counts = BW_STORAGE_BUFFER(CullingDispatch.instance_groups).Load2(address);
      BW_STORAGE_BUFFER(CullingDispatch.instance_groups).Store2(address, uint2(survivors, work));
      survivors += counts.x;
      work += counts.y;
    }
    g_survivors[s] = survivors;
    g_work[s] = work;
  }
  GroupMemoryBarrierWithGroupSync();
  if (lane != 0u) {
    return;
  }
  uint first_survivor = 0u;
  uint first_work = 0u;
  uint main_end = 0u;
  for (uint s = 0; s < segments; ++s) {
    BW_STORAGE_BUFFER(CullingDispatch.segments)
        .Store4(s * BW_SEGMENT_BYTES, uint4(first_survivor, g_survivors[s], first_work, g_work[s]));
    first_survivor += g_survivors[s];
    first_work = (first_work + g_work[s] + BW_CULL_GROUP - 1u) / BW_CULL_GROUP * BW_CULL_GROUP;
    if (s + 1u == pipelines) {
      main_end = first_work;
    }
  }
  BW_STORAGE_BUFFER(CullingDispatch.segments)
      .Store4(segments * BW_SEGMENT_BYTES, uint4(first_survivor, first_work, main_end, 0u));
  bw_write_dispatch(0u, first_work);
  bw_write_dispatch(12u, main_end);
}
