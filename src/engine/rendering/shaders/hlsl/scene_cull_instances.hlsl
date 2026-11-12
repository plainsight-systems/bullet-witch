// The culling dispatch's instance test (bw/rendering/scene/culling.hpp,
// contracts 2, 3 and 5; compute_shaders.hpp, contract 1): one thread per
// (instance slot, view), the dispatch (groups of instance slots, views, 1),
// phase one only.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// SceneCullInstances): compute, entry main, per-draw block CullingDispatch,
// buffer blocks InstanceRecord, LevelRecord and CullingView.
//
// A live instance with a pipeline index below pipeline_count passes a view
// when its current world box is not outside the view's planes. In the main
// view, with a usable history, its previous world box is then tested
// against the history under the previous main view; failing that it is
// still a survivor, marked occluded, so its clusters reach phase two. Its
// detail levels are chosen from the main view's two detail origins, and
// its candidate word records them with the work items (clusters) they
// bring. Each group then writes, per pipeline, how many survivors and work
// items its slots give (bw/scene/culling.hlsli, the scratch layouts).
// Every view is tested by one dispatch.
#include "bw/bindless.hlsli"
#include "bw/generated/scene_cull_instances.hlsli"
#include "bw/scene/culling.hlsli"

// detail_level.hpp, contract 1: the square of the fraction of half the
// viewport the sphere's radius covers.
float bw_projected_radius_squared(float distance_squared, float radius, float scale) {
  const float scaled = scale * radius;
  return (scaled * scaled) / max(1.0, distance_squared);
}

// detail_level.hpp, contract 2: the highest level whose half screen size,
// squared, reaches the projection, scanning from the last; 0 otherwise.
uint bw_select_level(uint first_level, uint level_count, float projected) {
  if (level_count <= 1u) {
    return 0u;
  }
  for (uint k = level_count; k > 0u; --k) {
    const BW_LevelRecord level =
        bw_load_LevelRecord(CullingDispatch.levels, (first_level + k - 1u) * BW_LEVEL_RECORD_BYTES);
    const float half_size = level.screen_size * 0.5;
    if (half_size * half_size >= projected) {
      return k - 1u;
    }
  }
  return 0u;
}

uint bw_level_clusters(uint first_level, uint level) {
  return bw_load_LevelRecord(CullingDispatch.levels, (first_level + level) * BW_LEVEL_RECORD_BYTES)
      .cluster_count;
}

groupshared uint g_pipeline[BW_CULL_GROUP];
groupshared uint g_work[BW_CULL_GROUP];

[numthreads(64, 1, 1)] void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
  const uint slot = group.x * BW_CULL_GROUP + lane;
  const uint view_index = group.y;
  uint pipeline = BW_NONE;
  uint work = 0u;
  if (slot < CullingDispatch.instance_capacity) {
    const BW_InstanceRecord instance =
        bw_load_InstanceRecord(CullingDispatch.instances, slot * BW_INSTANCE_RECORD_BYTES);
    uint flags = 0u;
    uint lower = 0u;
    uint higher = 0u;
    float lower_weight = 0.0;
    float higher_weight = 0.0;
    if ((instance.kind_and_flags & BW_INSTANCE_LIVE) != 0u &&
        instance.pipeline < CullingDispatch.pipeline_count) {
      const BW_CullingView view =
          bw_load_CullingView(CullingDispatch.views, view_index * BW_CULLING_VIEW_BYTES);
      if (!bw_box_outside(view.planes, instance.bounds_center, instance.bounds_extent)) {
        flags = BW_CANDIDATE_PASS;
        if (view_index == 0u && CullingDispatch.phase == 0u &&
            (CullingDispatch.flags & BW_CULLING_HISTORY_USABLE) != 0u &&
            !bw_hierarchy_visible(instance.previous_bounds_center, instance.previous_bounds_extent,
                                  CullingDispatch.previous_world_to_clip, CullingDispatch.hierarchy,
                                  uint2(CullingDispatch.hierarchy_width,
                                        CullingDispatch.hierarchy_height),
                                  CullingDispatch.hierarchy_levels,
                                  float2(view.viewport_width, view.viewport_height))) {
          flags |= BW_CANDIDATE_OCCLUDED;
        }
        // Every view draws the levels the main view chose (culling.hpp,
        // contract 5).
        if (instance.level_count > 1u) {
          const BW_CullingView main_view = bw_load_CullingView(CullingDispatch.views, 0u);
          const float radius = length(instance.bounds_extent);
          const float3 to_older = instance.bounds_center - main_view.detail_origin_older;
          const float3 to_newer = instance.bounds_center - main_view.detail_origin_newer;
          const uint older = bw_select_level(
              instance.first_level, instance.level_count,
              bw_projected_radius_squared(dot(to_older, to_older), radius,
                                          main_view.projection_scale));
          const uint newer = bw_select_level(
              instance.first_level, instance.level_count,
              bw_projected_radius_squared(dot(to_newer, to_newer), radius,
                                          main_view.projection_scale));
          // detail_level.hpp, contract 3: older fades out with the
          // transition, newer fades in with the transition minus one.
          if (older != newer) {
            flags |= BW_CANDIDATE_TWO_LEVELS;
            const float t = main_view.detail_transition;
            lower = min(older, newer);
            higher = max(older, newer);
            lower_weight = lower == older ? t : t - 1.0;
            higher_weight = higher == older ? t : t - 1.0;
          } else {
            lower = newer;
            higher = newer;
          }
        }
        work = bw_level_clusters(instance.first_level, lower);
        if ((flags & BW_CANDIDATE_TWO_LEVELS) != 0u) {
          work += bw_level_clusters(instance.first_level, higher);
        }
        pipeline = instance.pipeline;
      }
    }
    const uint address =
        (view_index * CullingDispatch.instance_capacity + slot) * BW_CANDIDATE_BYTES;
    BW_STORAGE_BUFFER(CullingDispatch.candidates)
        .Store4(address, uint4(flags | (lower << 8) | (higher << 12), asuint(lower_weight),
                               asuint(higher_weight), work));
  }
  g_pipeline[lane] = pipeline;
  g_work[lane] = work;
  GroupMemoryBarrierWithGroupSync();
  // One lane per pipeline (kMaxPipelines is the group size): the group's
  // survivors and work items of that pipeline.
  if (lane < CullingDispatch.pipeline_count) {
    uint survivors = 0u;
    uint items = 0u;
    for (uint k = 0; k < BW_CULL_GROUP; ++k) {
      if (g_pipeline[k] == lane) {
        survivors += 1u;
        items += g_work[k];
      }
    }
    const uint entry =
        (view_index * bw_instance_groups_per_view() + group.x) * CullingDispatch.pipeline_count +
        lane;
    BW_STORAGE_BUFFER(CullingDispatch.instance_groups)
        .Store2(entry * BW_INSTANCE_GROUP_BYTES, uint2(survivors, items));
  }
}
