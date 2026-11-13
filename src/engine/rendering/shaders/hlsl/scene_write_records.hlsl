// The culling dispatch's record write (bw/rendering/scene/culling.hpp,
// contracts 1, 4 and 6; draw_records.hpp, contract 1; compute_shaders.hpp,
// contract 1): an indirect dispatch, one thread per work item, as the
// cluster test.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// SceneWriteRecords): compute, entry main, per-draw block CullingDispatch,
// buffer blocks CullSurvivor, ClusterRecord, CullingList and DrawRecord.
//
// A passing work item's place in its list is its group's prefix plus the
// passing work items in lower lanes of the group: the list's records are in
// instance order and, within an instance, in level then cluster table
// order, and record i carries draw_index i. Phase one writes list (view,
// pipeline, PhaseOne), phase two the main view's PhaseTwo list. Each field
// of the DrawRecord is stored at the offset draw_records.hpp states.
#include "bw/bindless.hlsli"
#include "bw/generated/scene_write_records.hlsli"
#include "bw/scene/culling.hlsli"

groupshared uint g_pass[BW_CULL_GROUP];

[numthreads(64, 1, 1)] void main(uint3 group : SV_GroupID, uint lane : SV_GroupIndex) {
  const uint3 totals = bw_segment_totals();
  const bool phase_two = CullingDispatch.phase != 0u;
  const uint items = phase_two ? totals.z : totals.y;
  const uint item = bw_work_item(group, lane, items);
  uint pass = 0u;
  if (item < items) {
    pass = BW_BUFFER(CullingDispatch.cluster_state_read).Load(item * 4u) & BW_CLUSTER_PASS;
  }
  g_pass[lane] = pass;
  GroupMemoryBarrierWithGroupSync();
  if (pass == 0u) {
    return;
  }
  uint rank = 0u;
  for (uint k = 0; k < lane; ++k) {
    rank += g_pass[k];
  }
  const uint position =
      BW_BUFFER(CullingDispatch.cluster_groups_read).Load((item / BW_CULL_GROUP) * 4u) + rank;
  const BW_CullSurvivor survivor = bw_load_CullSurvivor(
      CullingDispatch.survivors_read, bw_find_survivor(item, totals.x) * BW_CULL_SURVIVOR_BYTES);
  const uint local = item - survivor.work_first;
  const bool lower = local < survivor.cluster_count0;
  const uint cluster_index = lower ? survivor.first_cluster0 + local
                                   : survivor.first_cluster1 + (local - survivor.cluster_count0);
  const BW_ClusterRecord cluster =
      bw_load_ClusterRecord(CullingDispatch.clusters, cluster_index * BW_CLUSTER_RECORD_BYTES);
  const uint pipeline = survivor.segment % CullingDispatch.pipeline_count;
  const uint table_index = phase_two ? bw_segment_count() + pipeline : survivor.segment;
  const BW_CullingList target =
      bw_load_CullingList(CullingDispatch.lists, table_index * BW_CULLING_LIST_BYTES);
  // DrawRecord: draw_index 0, index_count 4, instance_count 8,
  // first_index 12, base_vertex 16, first_instance 20, instance 24, bone
  // 28, detail_level 32, transition 36, two reserved words.
  const uint address = (target.first_record + position) * BW_DRAW_RECORD_BYTES;
  RWByteAddressBuffer records = BW_STORAGE_BUFFER(CullingDispatch.records);
  records.Store4(address, uint4(position, cluster.index_count, 1u, cluster.first_index));
  records.Store4(address + 16u, uint4(0u, 0u, survivor.instance, cluster.bone));
  records.Store4(address + 32u, uint4(lower ? survivor.level0 : survivor.level1,
                                      asuint(lower ? survivor.weight0 : survivor.weight1), 0u,
                                      0u));
}
