// The culling dispatch's survivor write (bw/rendering/scene/culling.hpp,
// contract 6; compute_shaders.hpp, contract 1): one thread per (instance
// slot, view), as the instance test, phase one only.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// SceneWriteSurvivors): compute, entry main, per-draw block
// CullingDispatch, buffer blocks InstanceRecord, LevelRecord, CullingView
// and CullSurvivor.
//
// A slot that passed its view is written as a CullSurvivor at its
// segment's first survivor, plus its group's prefix, plus the survivors of
// its pipeline in lower lanes of the group, so a segment's survivors are in
// instance order. Its first work item is found the same way from the work
// counts. Each field is stored at the offset compute_shaders.hpp states.
#include "bw/bindless.hlsli"
#include "bw/generated/scene_write_survivors.hlsli"
#include "bw/scene/culling.hlsli"

groupshared uint g_pipeline[BW_CULL_GROUP];
groupshared uint g_work[BW_CULL_GROUP];

[numthreads(64, 1, 1)] void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
  const uint slot = group.x * BW_CULL_GROUP + lane;
  const uint view_index = group.y;
  uint4 candidate = uint4(0u, 0u, 0u, 0u);
  uint pipeline = BW_NONE;
  BW_InstanceRecord instance = (BW_InstanceRecord)0;
  if (slot < CullingDispatch.instance_capacity) {
    candidate = BW_BUFFER(CullingDispatch.candidates_read)
                    .Load4((view_index * CullingDispatch.instance_capacity + slot) *
                           BW_CANDIDATE_BYTES);
    if ((candidate.x & BW_CANDIDATE_PASS) != 0u) {
      instance = bw_load_InstanceRecord(CullingDispatch.instances, slot * BW_INSTANCE_RECORD_BYTES);
      pipeline = instance.pipeline;
    }
  }
  g_pipeline[lane] = pipeline;
  g_work[lane] = pipeline == BW_NONE ? 0u : candidate.w;
  GroupMemoryBarrierWithGroupSync();
  if (pipeline == BW_NONE) {
    return;
  }
  uint rank = 0u;
  uint work_before = 0u;
  for (uint k = 0; k < lane; ++k) {
    if (g_pipeline[k] == pipeline) {
      rank += 1u;
      work_before += g_work[k];
    }
  }
  const uint segment = view_index * CullingDispatch.pipeline_count + pipeline;
  const uint4 placed = BW_BUFFER(CullingDispatch.segments_read).Load4(segment * BW_SEGMENT_BYTES);
  const uint entry =
      (view_index * bw_instance_groups_per_view() + group.x) * CullingDispatch.pipeline_count +
      pipeline;
  const uint2 prefix =
      BW_BUFFER(CullingDispatch.instance_groups_read).Load2(entry * BW_INSTANCE_GROUP_BYTES);
  const uint index = placed.x + prefix.x + rank;
  const uint work_first = placed.z + prefix.y + work_before;

  const uint lower = (candidate.x >> 8) & 0xFu;
  const uint higher = (candidate.x >> 12) & 0xFu;
  const bool two = (candidate.x & BW_CANDIDATE_TWO_LEVELS) != 0u;
  const BW_LevelRecord lower_level = bw_load_LevelRecord(
      CullingDispatch.levels, (instance.first_level + lower) * BW_LEVEL_RECORD_BYTES);
  const BW_LevelRecord higher_level = bw_load_LevelRecord(
      CullingDispatch.levels, (instance.first_level + higher) * BW_LEVEL_RECORD_BYTES);
  const uint flags = (candidate.x & BW_CANDIDATE_OCCLUDED) != 0u ? BW_SURVIVOR_OCCLUDED : 0u;
  // CullSurvivor: instance 0, segment 4, work_first 8, flags 12,
  // first_cluster0 16, cluster_count0 20, level0 24, weight0 28,
  // first_cluster1 32, cluster_count1 36, level1 40, weight1 44.
  const uint address = index * BW_CULL_SURVIVOR_BYTES;
  RWByteAddressBuffer survivors = BW_STORAGE_BUFFER(CullingDispatch.survivors);
  survivors.Store4(address, uint4(slot, segment, work_first, flags));
  survivors.Store4(address + 16u, uint4(lower_level.first_cluster, lower_level.cluster_count,
                                        lower, candidate.y));
  survivors.Store4(address + 32u,
                   uint4(higher_level.first_cluster, two ? higher_level.cluster_count : 0u,
                         higher, candidate.z));
}
