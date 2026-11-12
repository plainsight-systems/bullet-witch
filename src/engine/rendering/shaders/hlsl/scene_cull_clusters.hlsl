// The culling dispatch's cluster test (bw/rendering/scene/culling.hpp,
// contracts 2 to 4; compute_shaders.hpp, contract 1): an indirect dispatch,
// one thread per work item, over every work item in phase one and over the
// main view's in phase two.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// SceneCullClusters): compute, entry main, per-draw block CullingDispatch,
// buffer blocks InstanceRecord, ClusterRecord, CullingView and
// CullSurvivor.
//
// A work item is the cluster of its survivor's lower level and then of the
// higher, in cluster table order. Phase one: the cluster's box, its cooked
// box under the instance's transform grown by max_displacement, fails when
// it is outside its view's planes; in the main view with a usable history,
// a cluster in the frustum is rejected by occlusion when its survivor is
// marked occluded, or when its box under the previous transform fails the
// history under the previous main view. Phase two: a work item phase one
// rejected by occlusion passes when the instance's current box and then
// the cluster's pass the first build under the current main view. Each
// work item's state and each group's passing count are written.
// The record's bone transform is not applied to a cluster's box (scene.md,
// "The instance"): bone_first is kNone in every record until the geometry
// feed writes one (instance.hpp, contract 2).
#include "bw/bindless.hlsli"
#include "bw/generated/scene_cull_clusters.hlsli"
#include "bw/scene/culling.hlsli"

groupshared uint g_pass[BW_CULL_GROUP];

[numthreads(64, 1, 1)] void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
  const uint3 totals = bw_segment_totals();
  const bool phase_two = CullingDispatch.phase != 0u;
  const uint items = phase_two ? totals.z : totals.y;
  const uint item = bw_work_item(group, lane, items);
  uint pass = 0u;
  if (item < items) {
    uint state = 0u;
    const bool retest =
        !phase_two ||
        (BW_STORAGE_BUFFER(CullingDispatch.cluster_state).Load(item * 4u) & BW_CLUSTER_REJECTED) !=
            0u;
    const uint found = retest ? bw_find_survivor(item, totals.x) : BW_NONE;
    if (found != BW_NONE) {
      const BW_CullSurvivor survivor =
          bw_load_CullSurvivor(CullingDispatch.survivors_read, found * BW_CULL_SURVIVOR_BYTES);
      const uint local = item - survivor.work_first;
      if (local < survivor.cluster_count0 + survivor.cluster_count1) {
        const uint cluster_index = local < survivor.cluster_count0
                                       ? survivor.first_cluster0 + local
                                       : survivor.first_cluster1 + (local - survivor.cluster_count0);
        const BW_ClusterRecord cluster = bw_load_ClusterRecord(
            CullingDispatch.clusters, cluster_index * BW_CLUSTER_RECORD_BYTES);
        const BW_InstanceRecord instance = bw_load_InstanceRecord(
            CullingDispatch.instances, survivor.instance * BW_INSTANCE_RECORD_BYTES);
        const uint view_index = survivor.segment / CullingDispatch.pipeline_count;
        const BW_CullingView view =
            bw_load_CullingView(CullingDispatch.views, view_index * BW_CULLING_VIEW_BYTES);
        const uint2 hierarchy_size =
            uint2(CullingDispatch.hierarchy_width, CullingDispatch.hierarchy_height);
        const float2 viewport = float2(view.viewport_width, view.viewport_height);
        float3 center;
        float3 extent;
        bw_world_box(cluster.center, cluster.extent, instance.object_to_world,
                     instance.max_displacement, center, extent);
        if (!phase_two) {
          if (!bw_box_outside(view.planes, center, extent)) {
            bool rejected = false;
            if (view_index == 0u && (CullingDispatch.flags & BW_CULLING_HISTORY_USABLE) != 0u) {
              if ((survivor.flags & BW_SURVIVOR_OCCLUDED) != 0u) {
                rejected = true;
              } else {
                float3 previous_center;
                float3 previous_extent;
                bw_world_box(cluster.center, cluster.extent, instance.previous_object_to_world,
                             instance.max_displacement, previous_center, previous_extent);
                rejected = !bw_hierarchy_visible(
                    previous_center, previous_extent, CullingDispatch.previous_world_to_clip,
                    CullingDispatch.hierarchy, hierarchy_size, CullingDispatch.hierarchy_levels,
                    viewport);
              }
            }
            state = rejected ? BW_CLUSTER_REJECTED : BW_CLUSTER_PASS;
          }
        } else if (bw_hierarchy_visible(instance.bounds_center, instance.bounds_extent,
                                        view.world_to_clip, CullingDispatch.hierarchy,
                                        hierarchy_size, CullingDispatch.hierarchy_levels,
                                        viewport) &&
                   bw_hierarchy_visible(center, extent, view.world_to_clip,
                                        CullingDispatch.hierarchy, hierarchy_size,
                                        CullingDispatch.hierarchy_levels, viewport)) {
          state = BW_CLUSTER_PASS;
        }
      }
    }
    BW_STORAGE_BUFFER(CullingDispatch.cluster_state).Store(item * 4u, state);
    pass = state & BW_CLUSTER_PASS;
  }
  g_pass[lane] = pass;
  GroupMemoryBarrierWithGroupSync();
  if (lane == 0u && item < items) {
    uint count = 0u;
    for (uint k = 0; k < BW_CULL_GROUP; ++k) {
      count += g_pass[k];
    }
    BW_STORAGE_BUFFER(CullingDispatch.cluster_groups).Store((item / BW_CULL_GROUP) * 4u, count);
  }
}
