#ifndef BW_RENDERING_SCENE_DRAW_RECORDS_HPP
#define BW_RENDERING_SCENE_DRAW_RECORDS_HPP

#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/materials/shading.hpp"
#include "bw/rendering/scene/instance.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Responsibility: what the culling dispatch writes and a pass draws
// (docs/architecture/scene.md, "Draw records and submission", invariants 7,
// 8 and 14): the draw record, one per visible cluster; the pipeline table
// that gives each opaque or masked material class an index; and the layout
// of a frame's record lists, one per view per pipeline, in one record
// buffer with one count each in one count buffer.
// A list is drawn by one multi-draw over its compacted per-cluster
// records, and draws are grouped by material and render state: a merged
// draw shares its pipeline, bindings and index buffer. The five argument
// words of each draw are written on the GPU. A list is already one
// pipeline, and its order is the instance and cluster order, so opaque
// draws carry no sort key. The record carries the instance index itself,
// so no instance-id buffer sits beside the arguments.
//
// Contract.
//   1. DrawRecord, 48 bytes, in the parameter block layout: the leading 24
//      bytes are the RHI's IndirectDrawRecord (bw/rendering/rhi/
//      command_list.hpp; culling.hpp asserts the two agree; this header
//      names no RHI type because the shader registry reads it where the
//      RHI is not built): the draw's index in its list,
//      then index_count, instance_count 1, first_index in the scene's
//      index buffer, base_vertex 0, first_instance 0; then the instance
//      index, the cluster's bone or kNone, the detail level and the
//      transition weight of detail_level.hpp, contract 3.
//   2. RecordDraw is the per-draw block of every shader that draws
//      records: the record buffer's index and the byte offset of the list
//      being drawn, and the instance buffer's index. A vertex shader loads
//      the DrawRecord at list_offset + draw index * kDrawRecordBytes and
//      the InstanceRecord its instance names. A draw the CPU issues binds
//      a list of one record and draws index 0, so one shader serves both.
//   3. PipelineTable. index_of(key) is the key's index, the same for equal
//      keys, assigned in first-use order from 0; no value when the table
//      holds kMaxPipelines other keys. keys() is the keys by index.
//      Precondition: the key's blend mode is Opaque or Masked; a blended
//      material's draws are the CPU's (cpu_order.hpp).
//   4. make_draw_list_layout(view_count, pipeline_clusters) places the
//      lists of a frame. pipeline_clusters[p] is the capacity of every
//      list of pipeline p: the clusters of every live instance with that
//      index (InstanceSet::cluster_capacity). A list exists for every
//      (view, pipeline), and the main view has two per pipeline, PhaseOne
//      and PhaseTwo; a view other than the main view has PhaseOne only.
//      No two lists overlap; record_bytes and count_bytes are the two
//      buffers' sizes; every list's capacity holds every cluster that can
//      reach it, so the dispatch cannot run out of room.
//      list(view, pipeline, phase) is that list's place. Precondition:
//      view below view_count, pipeline below pipeline_clusters.size(), and
//      PhaseTwo only for kMainView.
//
// Core guidelines read: I.1, Con.5, F.20, C.131. Performance guidelines
// read: GDSA.10 (survivors written densely, the count kept on the device,
// the order declared stable), GPU.9 (one buffer, ranges inside it),
// CACHE.2.
namespace bw::scene {

struct alignas(shaders::kBlockAlignment) DrawRecord {
  std::uint32_t draw_index{};
  std::uint32_t index_count{};
  std::uint32_t instance_count{1};
  std::uint32_t first_index{};
  std::int32_t base_vertex{};
  std::uint32_t first_instance{};
  std::uint32_t instance{};
  std::uint32_t bone{kNone};
  std::uint32_t detail_level{};
  float transition{};
  std::uint32_t reserved0{};
  std::uint32_t reserved1{};
};
inline constexpr std::uint32_t kDrawRecordBytes = 48;
static_assert(sizeof(DrawRecord) == kDrawRecordBytes);

struct alignas(shaders::kBlockAlignment) RecordDraw {
  shaders::BufferIndex records{};
  std::uint32_t list_offset{};
  shaders::BufferIndex instances{};
  std::uint32_t reserved{};
};
inline constexpr std::uint32_t kRecordDrawBytes = 16;
static_assert(sizeof(RecordDraw) == kRecordDrawBytes);

inline constexpr std::uint32_t kMaxPipelines = 64;

struct PipelineKey {
  const materials::MaterialTemplate* material_template{};
  shaders::PermutationId permutation{};
  materials::BlendMode blend_mode{materials::BlendMode::Opaque};
  [[nodiscard]] friend constexpr bool operator==(const PipelineKey&,
                                                 const PipelineKey&) noexcept = default;
};

class PipelineTable {
public:
  [[nodiscard]] std::optional<std::uint32_t> index_of(const PipelineKey& key);
  [[nodiscard]] std::span<const PipelineKey> keys() const noexcept { return keys_; }

private:
  std::vector<PipelineKey> keys_{};
};

enum class DrawPhase : std::uint8_t { PhaseOne, PhaseTwo };

struct DrawList {
  // Byte offset of the list's first record in the record buffer.
  std::uint64_t record_offset{};
  // Byte offset of the list's 32-bit count in the count buffer.
  std::uint64_t count_offset{};
  // Records the list can hold.
  std::uint32_t capacity{};
};

struct DrawListLayout {
  std::uint32_t view_count{};
  std::uint32_t pipeline_count{};
  std::uint64_t record_bytes{};
  std::uint64_t count_bytes{};
  // Per pipeline: its capacity and the record offset of its list within
  // one view's block of lists.
  std::vector<std::uint32_t> capacities{};
  std::vector<std::uint64_t> offsets{};

  [[nodiscard]] DrawList list(std::uint32_t view, std::uint32_t pipeline,
                              DrawPhase phase) const noexcept;
};

[[nodiscard]] DrawListLayout
make_draw_list_layout(std::uint32_t view_count, std::span<const std::uint32_t> pipeline_clusters);

} // namespace bw::scene

using DrawRecord = bw::scene::DrawRecord;
BW_PARAMETER_BLOCK_LAYOUT(
    DrawRecord, BW_PARAMETER_MEMBER(DrawRecord, draw_index),
    BW_PARAMETER_MEMBER(DrawRecord, index_count), BW_PARAMETER_MEMBER(DrawRecord, instance_count),
    BW_PARAMETER_MEMBER(DrawRecord, first_index), BW_PARAMETER_MEMBER(DrawRecord, base_vertex),
    BW_PARAMETER_MEMBER(DrawRecord, first_instance), BW_PARAMETER_MEMBER(DrawRecord, instance),
    BW_PARAMETER_MEMBER(DrawRecord, bone), BW_PARAMETER_MEMBER(DrawRecord, detail_level),
    BW_PARAMETER_MEMBER(DrawRecord, transition), BW_PARAMETER_MEMBER(DrawRecord, reserved0),
    BW_PARAMETER_MEMBER(DrawRecord, reserved1));
using RecordDraw = bw::scene::RecordDraw;
BW_PARAMETER_BLOCK_LAYOUT(RecordDraw, BW_PARAMETER_MEMBER(RecordDraw, records),
                          BW_PARAMETER_MEMBER(RecordDraw, list_offset),
                          BW_PARAMETER_MEMBER(RecordDraw, instances),
                          BW_PARAMETER_MEMBER(RecordDraw, reserved));

#endif
