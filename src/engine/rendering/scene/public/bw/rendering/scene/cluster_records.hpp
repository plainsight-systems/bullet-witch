#ifndef BW_RENDERING_SCENE_CLUSTER_RECORDS_HPP
#define BW_RENDERING_SCENE_CLUSTER_RECORDS_HPP

#include "bw/rendering/scene/instance.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <cstdint>

// Responsibility: the two records of the scene's mesh tables as the GPU
// holds them (docs/architecture/scene.md, "The instance", the Mesh row): a
// cluster and a detail level, in the parameter block layout so C++ and
// HLSL read the same bytes. scene_geometry.hpp fills and uploads the
// tables; this header names nothing of the RHI, so the shader registry
// reads it.
//
// Contract.
//   1. ClusterRecord: first_index and index_count are the cluster's range
//      in the scene's index buffer; bone is the skeleton bone position its
//      draw is bound whole to, or kNone; center and extent are its box in
//      the mesh's space.
//   2. LevelRecord: one detail level of one draw: its run of clusters in
//      the cluster table, empty when the level holds none of the draw, and
//      the screen size the level is chosen at (detail_level.hpp).
//
// Read guidance: CACHE.2 and CACHE.5 (the layouts are stated and
// asserted), C.131.
namespace bw::scene {

// Offsets: first_index 0, index_count 4, bone 8, reserved 12, center 16,
// extent 32; size 48.
struct alignas(shaders::kBlockAlignment) ClusterRecord {
  std::uint32_t first_index{};
  std::uint32_t index_count{};
  std::uint32_t bone{kNone};
  std::uint32_t reserved{};
  shaders::Float3 center{};
  shaders::Float3 extent{};
};
inline constexpr std::uint32_t kClusterRecordBytes = 48;
static_assert(sizeof(ClusterRecord) == kClusterRecordBytes);

// Offsets: first_cluster 0, cluster_count 4, screen_size 8, reserved 12;
// size 16.
struct alignas(shaders::kBlockAlignment) LevelRecord {
  std::uint32_t first_cluster{};
  std::uint32_t cluster_count{};
  float screen_size{};
  std::uint32_t reserved{};
};
inline constexpr std::uint32_t kLevelRecordBytes = 16;
static_assert(sizeof(LevelRecord) == kLevelRecordBytes);

} // namespace bw::scene

using ClusterRecord = bw::scene::ClusterRecord;
BW_PARAMETER_BLOCK_LAYOUT(ClusterRecord, BW_PARAMETER_MEMBER(ClusterRecord, first_index),
                          BW_PARAMETER_MEMBER(ClusterRecord, index_count),
                          BW_PARAMETER_MEMBER(ClusterRecord, bone),
                          BW_PARAMETER_MEMBER(ClusterRecord, reserved),
                          BW_PARAMETER_MEMBER(ClusterRecord, center),
                          BW_PARAMETER_MEMBER(ClusterRecord, extent));
using LevelRecord = bw::scene::LevelRecord;
BW_PARAMETER_BLOCK_LAYOUT(LevelRecord, BW_PARAMETER_MEMBER(LevelRecord, first_cluster),
                          BW_PARAMETER_MEMBER(LevelRecord, cluster_count),
                          BW_PARAMETER_MEMBER(LevelRecord, screen_size),
                          BW_PARAMETER_MEMBER(LevelRecord, reserved));

#endif
