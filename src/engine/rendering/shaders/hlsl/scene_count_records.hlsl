// The culling dispatch's count pass (bw/rendering/scene/culling.hpp,
// contracts 1, 4 and 6; compute_shaders.hpp, contract 1): one thread per
// list of the phase, every (view, pipeline) list in phase one and the main
// view's PhaseTwo lists in phase two.
//
// Registry entry (src/engine/rendering/shaders/permutation.cpp,
// SceneCountRecords): compute, entry main, per-draw block CullingDispatch,
// buffer block CullingList.
//
// The list's segment holds whole groups of work items. Their passing counts
// become exclusive prefixes in place, in group order, and the total is the
// list's count, written at the list's count word: the count the counted
// multi-draw reads (draw_records.hpp, DrawList::count_offset).
#include "bw/bindless.hlsli"
#include "bw/generated/scene_count_records.hlsli"
#include "bw/scene/culling.hlsli"

[numthreads(64, 1, 1)] void main(uint3 thread : SV_DispatchThreadID) {
  const uint pipelines = CullingDispatch.pipeline_count;
  const bool phase_two = CullingDispatch.phase != 0u;
  const uint lists = phase_two ? pipelines : bw_segment_count();
  const uint list = thread.x;
  if (list >= lists) {
    return;
  }
  // In phase two list p is the main view's segment p.
  const uint segment = list;
  const uint4 placed = BW_BUFFER(CullingDispatch.segments_read).Load4(segment * BW_SEGMENT_BYTES);
  const uint first_group = placed.z / BW_CULL_GROUP;
  const uint group_count = (placed.w + BW_CULL_GROUP - 1u) / BW_CULL_GROUP;
  uint running = 0u;
  for (uint g = 0; g < group_count; ++g) {
    const uint address = (first_group + g) * 4u;
    const uint count = BW_STORAGE_BUFFER(CullingDispatch.cluster_groups).Load(address);
    BW_STORAGE_BUFFER(CullingDispatch.cluster_groups).Store(address, running);
    running += count;
  }
  const uint table_index = phase_two ? bw_segment_count() + list : list;
  const BW_CullingList target =
      bw_load_CullingList(CullingDispatch.lists, table_index * BW_CULLING_LIST_BYTES);
  BW_STORAGE_BUFFER(CullingDispatch.counts).Store(target.count_word * 4u, running);
}
