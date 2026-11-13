#ifndef BW_RENDERING_SCENE_COMPUTE_SHADERS_HPP
#define BW_RENDERING_SCENE_COMPUTE_SHADERS_HPP

#include "bw/rendering/scene/cluster_records.hpp"
#include "bw/rendering/scene/draw_records.hpp"
#include "bw/rendering/scene/frame_views.hpp"
#include "bw/rendering/scene/instance.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <cstdint>
#include <string_view>

// Responsibility: the scene's compute shaders as the engine declares them:
// the culling dispatch's kernels (culling.hpp) and the depth hierarchy's
// build kernel (depth_hierarchy.hpp), with their names, their blocks and
// the layout of the scratch buffers they share. The passes that run them
// are culling.hpp's and depth_hierarchy.hpp's; this header names nothing
// of the RHI or the render graph, so the shader registry, which is
// compiled at configure time, reads it.
// The culling kernels test each instance against the frustum, then against
// the previous hierarchy, and then test the survivors' clusters; every view
// is culled in one dispatch. Phase two retests what phase one culled
// against the refreshed hierarchy and draws it the same frame. There is
// one culling context and an instance is one draw, so a thread per
// instance and view is the work item. Every list is written by scans;
// scene.md invariant 7 forbids an atomic append.
// The hierarchy's level 0 is read from the depth target two by two with
// reads clamped to the view, and each next level is the farthest of two by
// two. A transition names a whole resource
// (bw/rendering/rhi/transitions.hpp) and a pass names a resource once
// (bw/rendering/graph/declaration.hpp, contract 4), so a level cannot be
// read sampled while the next is written in one pass, and a typed storage
// read of a 16-bit float is not assumed on Direct3D 12. The build therefore
// reduces in a raw scratch buffer and copies each level into the texture.
//
// Contract.
//   1. Culling kernels. Each is a compute shader with no permutation and no
//      static block, entry main, per-draw block CullingDispatch (one per
//      dispatch). Buffer blocks, at most shaders::kMaxBufferBlocks each:
//        kCullInstancesShader    InstanceRecord, LevelRecord, CullingView
//        kScanInstancesShader    none
//        kWriteSurvivorsShader   InstanceRecord, LevelRecord, CullingView,
//                                CullSurvivor
//        kCullClustersShader     InstanceRecord, ClusterRecord,
//                                CullingView, CullSurvivor
//        kCountRecordsShader     CullingList
//        kWriteRecordsShader     CullSurvivor, ClusterRecord, CullingList,
//                                DrawRecord
//      Every other buffer a kernel reads or writes is raw words whose
//      layout is the implementation's.
//   2. kDepthHierarchyReduceShader is a compute shader with no permutation
//      and no static or buffer block, entry main, per-draw block
//      DepthHierarchyReduce, one per level. Level 0 reads the depth
//      texture `depth`; a later level reads the level below from the
//      scratch buffer `levels`, where the block's source and destination
//      fields place each level's rows.
//   3. What is promised to a caller is culling.hpp's and
//      depth_hierarchy.hpp's and nothing here adds to it: how many kernels
//      a phase runs and in what order, their thread group sizes, the
//      layout of the scratch buffers and the alignments of the hierarchy's
//      scratch levels are the implementation's, stated beside the kernels,
//      and no test asserts them. This header states only what the shader
//      registry and the code that fills a block must agree on: the names
//      and the blocks' layouts.
//
// Core guidelines read: I.1 (every input of a kernel is in its block or a
// buffer it names), Con.5, C.131 (the blocks are plain data). Performance
// guidelines read: GDSA.10 (scan compaction, counts kept on the device,
// order stable), GPU.6 (every view in one dispatch), CACHE.2 and CACHE.5
// (every layout is stated and asserted).
namespace bw::scene {

inline constexpr std::string_view kCullInstancesShader = "scene_cull_instances";
inline constexpr std::string_view kScanInstancesShader = "scene_scan_instances";
inline constexpr std::string_view kWriteSurvivorsShader = "scene_write_survivors";
inline constexpr std::string_view kCullClustersShader = "scene_cull_clusters";
inline constexpr std::string_view kCountRecordsShader = "scene_count_records";
inline constexpr std::string_view kWriteRecordsShader = "scene_write_records";
inline constexpr std::string_view kDepthHierarchyReduceShader = "depth_hierarchy_reduce";

enum class CullingPhase : std::uint8_t { One = 0, Two = 1 };

// CullingDispatch::flags: phase one's previous hierarchy and view are
// usable (culling.hpp, contract 3).
inline constexpr std::uint32_t kCullingHistoryUsable = 1U << 0U;

// CullSurvivor::flags: the instance passed its frustum and failed
// occlusion in phase one.
inline constexpr std::uint32_t kSurvivorOccluded = 1U << 0U;

// Offsets: previous_world_to_clip 0; read indices instances 64, levels 68,
// clusters 72, views 76, lists 80, candidates_read 84,
// instance_groups_read 88, segments_read 92, survivors_read 96,
// cluster_state_read 100, cluster_groups_read 104, hierarchy 108; storage
// indices candidates 112, instance_groups 116, segments 120, survivors
// 124, cluster_state 128, cluster_groups 132, arguments 136, records 140,
// counts 144; instance_capacity 148, view_count 152, pipeline_count 156,
// phase 160, flags 164, hierarchy_width 168, hierarchy_height 172,
// hierarchy_levels 176, three reserved words; size 192.
// previous_world_to_clip is the previous main view's, read in phase one
// with the history; hierarchy is the history in phase one and the first
// build in phase two.
struct alignas(shaders::kBlockAlignment) CullingDispatch {
  shaders::Float4x4 previous_world_to_clip{};
  shaders::BufferIndex instances{};
  shaders::BufferIndex levels{};
  shaders::BufferIndex clusters{};
  shaders::BufferIndex views{};
  shaders::BufferIndex lists{};
  shaders::BufferIndex candidates_read{};
  shaders::BufferIndex instance_groups_read{};
  shaders::BufferIndex segments_read{};
  shaders::BufferIndex survivors_read{};
  shaders::BufferIndex cluster_state_read{};
  shaders::BufferIndex cluster_groups_read{};
  shaders::TextureIndex hierarchy{};
  shaders::StorageBufferIndex candidates{};
  shaders::StorageBufferIndex instance_groups{};
  shaders::StorageBufferIndex segments{};
  shaders::StorageBufferIndex survivors{};
  shaders::StorageBufferIndex cluster_state{};
  shaders::StorageBufferIndex cluster_groups{};
  shaders::StorageBufferIndex arguments{};
  shaders::StorageBufferIndex records{};
  shaders::StorageBufferIndex counts{};
  std::uint32_t instance_capacity{};
  std::uint32_t view_count{};
  std::uint32_t pipeline_count{};
  std::uint32_t phase{};
  std::uint32_t flags{};
  std::uint32_t hierarchy_width{};
  std::uint32_t hierarchy_height{};
  std::uint32_t hierarchy_levels{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
  std::uint32_t reserved2{};
};
inline constexpr std::uint32_t kCullingDispatchBytes = 192;
static_assert(sizeof(CullingDispatch) == kCullingDispatchBytes);

// One view of the frame as the kernels read it (frame_views.hpp,
// SceneView), at its index in the views buffer. Offsets: world_to_clip 0,
// planes 64 (six, left, right, bottom, top, near, far, each (a, b, c, d)),
// detail_origin_older 160, detail_origin_newer 176, projection_scale 192,
// detail_transition 196, viewport_width 200, viewport_height 204; size
// 208.
struct alignas(shaders::kBlockAlignment) CullingView {
  shaders::Float4x4 world_to_clip{};
  shaders::Array<shaders::Float4, kFrustumPlanes> planes{};
  shaders::Float3 detail_origin_older{};
  shaders::Float3 detail_origin_newer{};
  float projection_scale{};
  float detail_transition{};
  float viewport_width{};
  float viewport_height{};
};
inline constexpr std::uint32_t kCullingViewBytes = 208;
static_assert(sizeof(CullingView) == kCullingViewBytes);

// One list as the kernels write it (draw_records.hpp, DrawList), at index
// view * pipeline_count + pipeline for PhaseOne, and view_count *
// pipeline_count + pipeline for the main view's PhaseTwo. Offsets:
// first_record 0 (record_offset / kDrawRecordBytes), count_word 4
// (count_offset / 4), capacity 8, reserved 12; size 16.
struct alignas(shaders::kBlockAlignment) CullingList {
  std::uint32_t first_record{};
  std::uint32_t count_word{};
  std::uint32_t capacity{};
  std::uint32_t reserved{};
};
inline constexpr std::uint32_t kCullingListBytes = 16;
static_assert(sizeof(CullingList) == kCullingListBytes);

// One instance that passed a view, with the work items it brings: the
// clusters of level0 then level1, each run in cluster table order.
// cluster_count1 is 0 when one level is drawn. Offsets: instance 0,
// segment 4 (view * pipeline_count + pipeline), work_first 8, flags 12,
// first_cluster0 16, cluster_count0 20, level0 24, weight0 28,
// first_cluster1 32, cluster_count1 36, level1 40, weight1 44; size 48.
// level0 is below level1 when both are drawn (culling.hpp, contract 6).
struct alignas(shaders::kBlockAlignment) CullSurvivor {
  std::uint32_t instance{};
  std::uint32_t segment{};
  std::uint32_t work_first{};
  std::uint32_t flags{};
  std::uint32_t first_cluster0{};
  std::uint32_t cluster_count0{};
  std::uint32_t level0{};
  float weight0{};
  std::uint32_t first_cluster1{};
  std::uint32_t cluster_count1{};
  std::uint32_t level1{};
  float weight1{};
};
inline constexpr std::uint32_t kCullSurvivorBytes = 48;
static_assert(sizeof(CullSurvivor) == kCullSurvivorBytes);

// Offsets: depth 0, levels 4, level 8, source_offset 12, source_pitch 16,
// source_width 20, source_height 24, destination_offset 28,
// destination_pitch 32, destination_width 36, destination_height 40,
// reserved 44; size 48. At level 0 the source is the depth texture and
// source_width and source_height are the view's size; source_offset and
// source_pitch are read from level 1 on.
struct alignas(shaders::kBlockAlignment) DepthHierarchyReduce {
  shaders::TextureIndex depth{};
  shaders::StorageBufferIndex levels{};
  std::uint32_t level{};
  std::uint32_t source_offset{};
  std::uint32_t source_pitch{};
  std::uint32_t source_width{};
  std::uint32_t source_height{};
  std::uint32_t destination_offset{};
  std::uint32_t destination_pitch{};
  std::uint32_t destination_width{};
  std::uint32_t destination_height{};
  std::uint32_t reserved{};
};
inline constexpr std::uint32_t kDepthHierarchyReduceBytes = 48;
static_assert(sizeof(DepthHierarchyReduce) == kDepthHierarchyReduceBytes);

} // namespace bw::scene

// The block layout macro stringizes its argument as the HLSL struct name,
// so each block is named unqualified at global scope, as every block is.
using CullingDispatch = bw::scene::CullingDispatch;
BW_PARAMETER_BLOCK_LAYOUT(
    CullingDispatch, BW_PARAMETER_MEMBER(CullingDispatch, previous_world_to_clip),
    BW_PARAMETER_MEMBER(CullingDispatch, instances), BW_PARAMETER_MEMBER(CullingDispatch, levels),
    BW_PARAMETER_MEMBER(CullingDispatch, clusters), BW_PARAMETER_MEMBER(CullingDispatch, views),
    BW_PARAMETER_MEMBER(CullingDispatch, lists),
    BW_PARAMETER_MEMBER(CullingDispatch, candidates_read),
    BW_PARAMETER_MEMBER(CullingDispatch, instance_groups_read),
    BW_PARAMETER_MEMBER(CullingDispatch, segments_read),
    BW_PARAMETER_MEMBER(CullingDispatch, survivors_read),
    BW_PARAMETER_MEMBER(CullingDispatch, cluster_state_read),
    BW_PARAMETER_MEMBER(CullingDispatch, cluster_groups_read),
    BW_PARAMETER_MEMBER(CullingDispatch, hierarchy),
    BW_PARAMETER_MEMBER(CullingDispatch, candidates),
    BW_PARAMETER_MEMBER(CullingDispatch, instance_groups),
    BW_PARAMETER_MEMBER(CullingDispatch, segments), BW_PARAMETER_MEMBER(CullingDispatch, survivors),
    BW_PARAMETER_MEMBER(CullingDispatch, cluster_state),
    BW_PARAMETER_MEMBER(CullingDispatch, cluster_groups),
    BW_PARAMETER_MEMBER(CullingDispatch, arguments), BW_PARAMETER_MEMBER(CullingDispatch, records),
    BW_PARAMETER_MEMBER(CullingDispatch, counts),
    BW_PARAMETER_MEMBER(CullingDispatch, instance_capacity),
    BW_PARAMETER_MEMBER(CullingDispatch, view_count),
    BW_PARAMETER_MEMBER(CullingDispatch, pipeline_count),
    BW_PARAMETER_MEMBER(CullingDispatch, phase), BW_PARAMETER_MEMBER(CullingDispatch, flags),
    BW_PARAMETER_MEMBER(CullingDispatch, hierarchy_width),
    BW_PARAMETER_MEMBER(CullingDispatch, hierarchy_height),
    BW_PARAMETER_MEMBER(CullingDispatch, hierarchy_levels),
    BW_PARAMETER_MEMBER(CullingDispatch, reserved0),
    BW_PARAMETER_MEMBER(CullingDispatch, reserved1),
    BW_PARAMETER_MEMBER(CullingDispatch, reserved2));
using CullingView = bw::scene::CullingView;
BW_PARAMETER_BLOCK_LAYOUT(CullingView, BW_PARAMETER_MEMBER(CullingView, world_to_clip),
                          BW_PARAMETER_MEMBER(CullingView, planes),
                          BW_PARAMETER_MEMBER(CullingView, detail_origin_older),
                          BW_PARAMETER_MEMBER(CullingView, detail_origin_newer),
                          BW_PARAMETER_MEMBER(CullingView, projection_scale),
                          BW_PARAMETER_MEMBER(CullingView, detail_transition),
                          BW_PARAMETER_MEMBER(CullingView, viewport_width),
                          BW_PARAMETER_MEMBER(CullingView, viewport_height));
using CullingList = bw::scene::CullingList;
BW_PARAMETER_BLOCK_LAYOUT(CullingList, BW_PARAMETER_MEMBER(CullingList, first_record),
                          BW_PARAMETER_MEMBER(CullingList, count_word),
                          BW_PARAMETER_MEMBER(CullingList, capacity),
                          BW_PARAMETER_MEMBER(CullingList, reserved));
using CullSurvivor = bw::scene::CullSurvivor;
BW_PARAMETER_BLOCK_LAYOUT(
    CullSurvivor, BW_PARAMETER_MEMBER(CullSurvivor, instance),
    BW_PARAMETER_MEMBER(CullSurvivor, segment), BW_PARAMETER_MEMBER(CullSurvivor, work_first),
    BW_PARAMETER_MEMBER(CullSurvivor, flags), BW_PARAMETER_MEMBER(CullSurvivor, first_cluster0),
    BW_PARAMETER_MEMBER(CullSurvivor, cluster_count0), BW_PARAMETER_MEMBER(CullSurvivor, level0),
    BW_PARAMETER_MEMBER(CullSurvivor, weight0), BW_PARAMETER_MEMBER(CullSurvivor, first_cluster1),
    BW_PARAMETER_MEMBER(CullSurvivor, cluster_count1), BW_PARAMETER_MEMBER(CullSurvivor, level1),
    BW_PARAMETER_MEMBER(CullSurvivor, weight1));
using DepthHierarchyReduce = bw::scene::DepthHierarchyReduce;
BW_PARAMETER_BLOCK_LAYOUT(DepthHierarchyReduce, BW_PARAMETER_MEMBER(DepthHierarchyReduce, depth),
                          BW_PARAMETER_MEMBER(DepthHierarchyReduce, levels),
                          BW_PARAMETER_MEMBER(DepthHierarchyReduce, level),
                          BW_PARAMETER_MEMBER(DepthHierarchyReduce, source_offset),
                          BW_PARAMETER_MEMBER(DepthHierarchyReduce, source_pitch),
                          BW_PARAMETER_MEMBER(DepthHierarchyReduce, source_width),
                          BW_PARAMETER_MEMBER(DepthHierarchyReduce, source_height),
                          BW_PARAMETER_MEMBER(DepthHierarchyReduce, destination_offset),
                          BW_PARAMETER_MEMBER(DepthHierarchyReduce, destination_pitch),
                          BW_PARAMETER_MEMBER(DepthHierarchyReduce, destination_width),
                          BW_PARAMETER_MEMBER(DepthHierarchyReduce, destination_height),
                          BW_PARAMETER_MEMBER(DepthHierarchyReduce, reserved));

#endif
