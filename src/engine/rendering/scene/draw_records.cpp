#include "bw/rendering/scene/draw_records.hpp"

#include "bw/rendering/scene/frame_views.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// Responsibility: the out-of-line part of bw/rendering/scene/draw_records.hpp.
//
// Readings of the header that the code encodes, the layout of contract 4:
//   A view's block of lists is every pipeline's list in pipeline order,
//   offsets[p] bytes from the block's start; a block holds the sum of the
//   capacities. The record buffer is the main view's PhaseOne block, its
//   PhaseTwo block, then one block per other view in view order. The count
//   buffer holds one 32-bit count per list in the same order. With no view
//   there are no lists and both sizes are 0.
namespace bw::scene {

// Draws are grouped by material and render state: equal keys share an
// index.
std::optional<std::uint32_t> PipelineTable::index_of(const PipelineKey& key) {
  const auto found = std::find(keys_.begin(), keys_.end(), key);
  if (found != keys_.end()) {
    return static_cast<std::uint32_t>(found - keys_.begin());
  }
  if (keys_.size() >= kMaxPipelines) {
    return std::nullopt;
  }
  keys_.push_back(key);
  return static_cast<std::uint32_t>(keys_.size() - 1U);
}

namespace {

// The block a list lives in: 0 the main view's PhaseOne, 1 its PhaseTwo,
// view + 1 every other view's.
std::uint64_t block_of(std::uint32_t view, DrawPhase phase) noexcept {
  if (view == kMainView) {
    return phase == DrawPhase::PhaseOne ? 0U : 1U;
  }
  return std::uint64_t{view} + 1U;
}

} // namespace

// The view and the pipeline in the order the header documents.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
DrawList DrawListLayout::list(std::uint32_t view, std::uint32_t pipeline,
                              DrawPhase phase) const noexcept {
  const std::uint64_t block = block_of(view, phase);
  const std::uint64_t block_bytes = view_count == 0U ? 0U : record_bytes / (view_count + 1U);
  DrawList list{};
  list.record_offset = (block * block_bytes) + offsets[pipeline];
  list.count_offset = ((block * pipeline_count) + pipeline) * sizeof(std::uint32_t);
  list.capacity = capacities[pipeline];
  return list;
}

// Each list is drawn by one multi-draw over its compacted per-cluster
// records.
DrawListLayout make_draw_list_layout(std::uint32_t view_count,
                                     std::span<const std::uint32_t> pipeline_clusters) {
  DrawListLayout layout{};
  layout.view_count = view_count;
  layout.pipeline_count = static_cast<std::uint32_t>(pipeline_clusters.size());
  layout.capacities.assign(pipeline_clusters.begin(), pipeline_clusters.end());
  layout.offsets.reserve(pipeline_clusters.size());
  std::uint64_t block_bytes = 0;
  for (const std::uint32_t capacity : pipeline_clusters) {
    layout.offsets.push_back(block_bytes);
    block_bytes += std::uint64_t{capacity} * kDrawRecordBytes;
  }
  const std::uint64_t blocks = view_count == 0U ? 0U : std::uint64_t{view_count} + 1U;
  layout.record_bytes = blocks * block_bytes;
  layout.count_bytes = blocks * layout.pipeline_count * sizeof(std::uint32_t);
  return layout;
}

} // namespace bw::scene
