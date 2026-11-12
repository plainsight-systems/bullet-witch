#include "bw/rendering/scene/instance_set.hpp"

#include "bw/math/matrix/matrix4.hpp"
#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/draw_records.hpp"
#include "bw/rendering/scene/instance.hpp"
#include "bw/rendering/scene/view.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Responsibility: the out-of-line part of bw/rendering/scene/instance_set.hpp.
//
// Readings of the header that the code encodes:
//   Free indices are taken lowest first: the free list is a stack filled
//   from the top index down, and a removed index is pushed back, so the
//   next entry reuses it.
//   An index's generation starts at 0 and steps on every remove; an id
//   names its instance while its generation is the index's.
//   changed() is kept ascending as indices are marked, so it can be read at
//   any time without a sort.
//   begin_frame walks the moved marks of [0, extent()), since the changed
//   list of the frame before was cleared by its upload.
//   declare_instance_upload packs the changed records into a new buffer
//   made with them as initial data, and the frame's Copy pass copies each
//   run of consecutive indices from it into the record buffer, so the CPU
//   never writes a buffer a frame in flight reads.
namespace bw::scene {

namespace {

constexpr std::uint8_t kChanged = 1U << 0U;
constexpr std::uint8_t kMoved = 1U << 1U;
constexpr std::uint32_t kPublishedFlags = kInstanceCarriesBakedLight | kInstanceCastsShadow;

struct CenterExtent {
  shaders::Float3 center;
  shaders::Float3 extent;
};

CenterExtent center_extent(const Box& box) noexcept {
  return {shaders::Float3{(box.min.x + box.max.x) * 0.5F, (box.min.y + box.max.y) * 0.5F,
                          (box.min.z + box.max.z) * 0.5F},
          shaders::Float3{(box.max.x - box.min.x) * 0.5F, (box.max.y - box.min.y) * 0.5F,
                          (box.max.z - box.min.z) * 0.5F}};
}

// Marks `index` changed, keeping the list ascending and unique.
void mark_changed(std::vector<std::uint32_t>& changed, std::vector<std::uint8_t>& marks,
                  std::uint32_t index) {
  if ((marks[index] & kChanged) != 0U) {
    return;
  }
  marks[index] = static_cast<std::uint8_t>(marks[index] | kChanged);
  changed.insert(std::lower_bound(changed.begin(), changed.end(), index), index);
}

} // namespace

InstanceSet::InstanceSet(std::uint32_t capacity)
    : records_(capacity), generations_(capacity, 0U), cluster_counts_(capacity, 0U),
      marks_(capacity, 0U), pipeline_clusters_(kMaxPipelines, 0U), local_bounds_(capacity) {
  free_.reserve(capacity);
  for (std::uint32_t i = capacity; i > 0U; --i) {
    free_.push_back(i - 1U);
  }
  changed_.reserve(capacity);
}

// An entered instance has its previous transform and box equal to its
// current ones.
InstanceError InstanceSet::enter(const InstanceDescription& description, InstanceId& id) {
  if (free_.empty()) {
    return InstanceError::SetFull;
  }
  const std::uint32_t index = free_.back();
  free_.pop_back();
  const InstanceGeometry& geometry = description.geometry;
  InstanceRecord record{};
  record.object_to_world = shader_matrix(description.transform);
  record.previous_object_to_world = record.object_to_world;
  const CenterExtent box = center_extent(
      world_box(geometry.bounds, description.transform, description.max_displacement));
  record.bounds_center = box.center;
  record.bounds_extent = box.extent;
  record.previous_bounds_center = record.bounds_center;
  record.previous_bounds_extent = record.bounds_extent;
  record.streams = geometry.streams;
  record.material = description.material;
  record.first_level = geometry.first_level;
  record.level_count = geometry.level_count;
  record.pipeline = description.pipeline;
  record.blend_mode = static_cast<std::uint32_t>(description.blend_mode);
  record.kind_and_flags = static_cast<std::uint32_t>(description.kind) |
                          (description.flags & kPublishedFlags) | kInstanceLive;
  record.sort_priority = description.sort_priority;
  record.max_displacement = description.max_displacement;
  records_[index] = record;
  cluster_counts_[index] = geometry.cluster_count;
  local_bounds_[index] = geometry.bounds;
  if (description.pipeline != kNone) {
    if (description.pipeline >= pipeline_clusters_.size()) {
      pipeline_clusters_.resize(std::size_t{description.pipeline} + 1U, 0U);
    }
    pipeline_clusters_[description.pipeline] += geometry.cluster_count;
    total_clusters_ += geometry.cluster_count;
  }
  extent_ = std::max(extent_, index + 1U);
  mark_changed(changed_, marks_, index);
  id = InstanceId{index, generations_[index]};
  return InstanceError::None;
}

InstanceError InstanceSet::move(InstanceId id, const math::Matrix4f& transform) {
  if (!id.valid() || id.index >= records_.size() || generations_[id.index] != id.generation ||
      (records_[id.index].kind_and_flags & kInstanceLive) == 0U) {
    return InstanceError::IdStale;
  }
  InstanceRecord& record = records_[id.index];
  record.object_to_world = shader_matrix(transform);
  const CenterExtent box =
      center_extent(world_box(local_bounds_[id.index], transform, record.max_displacement));
  record.bounds_center = box.center;
  record.bounds_extent = box.extent;
  marks_[id.index] = static_cast<std::uint8_t>(marks_[id.index] | kMoved);
  mark_changed(changed_, marks_, id.index);
  return InstanceError::None;
}

InstanceError InstanceSet::set_flags(InstanceId id, std::uint32_t flags) {
  if (!id.valid() || id.index >= records_.size() || generations_[id.index] != id.generation ||
      (records_[id.index].kind_and_flags & kInstanceLive) == 0U) {
    return InstanceError::IdStale;
  }
  std::uint32_t& word = records_[id.index].kind_and_flags;
  word = (word & ~kPublishedFlags) | (flags & kPublishedFlags);
  mark_changed(changed_, marks_, id.index);
  return InstanceError::None;
}

InstanceError InstanceSet::remove(InstanceId id) {
  if (!id.valid() || id.index >= records_.size() || generations_[id.index] != id.generation ||
      (records_[id.index].kind_and_flags & kInstanceLive) == 0U) {
    return InstanceError::IdStale;
  }
  const std::uint32_t index = id.index;
  const std::uint32_t pipeline = records_[index].pipeline;
  if (pipeline != kNone) {
    pipeline_clusters_[pipeline] -= cluster_counts_[index];
    total_clusters_ -= cluster_counts_[index];
  }
  cluster_counts_[index] = 0U;
  records_[index] = InstanceRecord{};
  local_bounds_[index] = Box{};
  // A reused index is not rolled by the next begin_frame.
  marks_[index] = static_cast<std::uint8_t>(marks_[index] & ~kMoved);
  mark_changed(changed_, marks_, index);
  ++generations_[index];
  free_.push_back(index);
  return InstanceError::None;
}

// Frame start sets previous to current and marks the record for upload.
void InstanceSet::begin_frame() {
  for (std::uint32_t i = 0; i < extent_; ++i) {
    if ((marks_[i] & kMoved) == 0U) {
      continue;
    }
    InstanceRecord& record = records_[i];
    record.previous_object_to_world = record.object_to_world;
    record.previous_bounds_center = record.bounds_center;
    record.previous_bounds_extent = record.bounds_extent;
    marks_[i] = static_cast<std::uint8_t>(marks_[i] & ~kMoved);
    mark_changed(changed_, marks_, i);
  }
}

std::uint32_t InstanceSet::capacity() const noexcept {
  return static_cast<std::uint32_t>(records_.size());
}
std::uint32_t InstanceSet::extent() const noexcept { return extent_; }
std::span<const InstanceRecord> InstanceSet::records() const noexcept {
  return std::span<const InstanceRecord>(records_).first(extent_);
}
std::span<const std::uint32_t> InstanceSet::changed() const noexcept { return changed_; }

void InstanceSet::clear_changed() noexcept {
  for (const std::uint32_t index : changed_) {
    marks_[index] = static_cast<std::uint8_t>(marks_[index] & ~kChanged);
  }
  changed_.clear();
}

std::uint32_t InstanceSet::cluster_capacity(std::uint32_t pipeline) const noexcept {
  return pipeline < pipeline_clusters_.size() ? pipeline_clusters_[pipeline] : 0U;
}
std::uint32_t InstanceSet::total_cluster_capacity() const noexcept { return total_clusters_; }

// A changed instance's record is uploaded whole.
InstanceUploadFailure declare_instance_upload(rhi::Device& device, InstanceSet& set,
                                              InstanceBuffer& buffer, graph::FrameGraph& graph) {
  // Every device object first, so a refusal declares nothing.
  if (!buffer.buffer) {
    rhi::BufferDescription description{};
    description.size = std::uint64_t{set.capacity()} * kInstanceRecordBytes;
    description.usage = rhi::BufferUsage::ShaderResource | rhi::BufferUsage::CopyDestination;
    description.initial_access = rhi::kShaderResourceMask;
    description.debug_name = "scene instance records";
    rhi::BufferResult created = device.create_buffer(description);
    if (!created.ok()) {
      return {created.error(), std::string(created.detail())};
    }
    rhi::ShaderResourceViewResult viewed =
        device.create_shader_resource_view(created.value(), rhi::BufferViewDescription{});
    if (!viewed.ok()) {
      return {viewed.error(), std::string(viewed.detail())};
    }
    buffer.buffer = std::move(created.value());
    buffer.view = std::move(viewed.value());
  }
  const std::span<const std::uint32_t> changed = set.changed();
  rhi::Ref<rhi::Buffer> upload;
  if (!changed.empty()) {
    const std::span<const InstanceRecord> records = set.records();
    std::vector<std::byte> packed(changed.size() * kInstanceRecordBytes);
    for (std::size_t k = 0; k < changed.size(); ++k) {
      std::memcpy(packed.data() + (k * kInstanceRecordBytes), &records[changed[k]],
                  kInstanceRecordBytes);
    }
    rhi::BufferDescription description{};
    description.size = packed.size();
    description.usage = rhi::BufferUsage::CopySource;
    description.initial_access = rhi::Access::CopySource;
    description.debug_name = "scene instance upload";
    rhi::BufferResult created = device.create_buffer(description, packed);
    if (!created.ok()) {
      return {created.error(), std::string(created.detail())};
    }
    upload = std::move(created.value());
  }

  buffer.handle = graph.import_buffer(graph::ImportedBuffer{
      buffer.buffer.get(), rhi::kShaderResourceMask, rhi::kShaderResourceMask});
  graph.mark_history(buffer.handle);
  buffer.runs.clear();
  buffer.upload = std::move(upload);
  buffer.upload_handle = {};
  if (changed.empty()) {
    return {};
  }
  // Runs of consecutive indices; the changed list is ascending.
  for (const std::uint32_t index : changed) {
    if (!buffer.runs.empty() && buffer.runs.back().first + buffer.runs.back().count == index) {
      ++buffer.runs.back().count;
    } else {
      buffer.runs.push_back(InstanceCopyRun{index, 1});
    }
  }
  buffer.upload_handle = graph.import_buffer(
      graph::ImportedBuffer{buffer.upload.get(), rhi::Access::CopySource, rhi::Access::CopySource});
  const std::array<graph::BufferUse, 2> uses{
      graph::BufferUse{buffer.handle, graph::Access::CopyDestination},
      graph::BufferUse{buffer.upload_handle, graph::Access::CopySource}};
  graph.add_pass(graph::PassDeclaration{
      "scene instance upload", graph::PassKind::Copy, {}, uses, buffer.record});
  set.clear_changed();
  return {};
}

void InstanceBuffer::Record::operator()(graph::RecordContext& context) const {
  rhi::Buffer* source = context.buffer(buffer->upload_handle);
  rhi::Buffer* destination = context.buffer(buffer->handle);
  std::uint64_t offset = 0;
  for (const InstanceCopyRun& run : buffer->runs) {
    const std::uint64_t bytes = std::uint64_t{run.count} * kInstanceRecordBytes;
    context.list().copy_buffer(
        *source, *destination,
        rhi::BufferCopy{offset, std::uint64_t{run.first} * kInstanceRecordBytes, bytes});
    offset += bytes;
  }
}

} // namespace bw::scene
