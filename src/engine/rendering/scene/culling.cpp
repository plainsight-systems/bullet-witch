#include "bw/rendering/scene/culling.hpp"

#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/compute_shaders.hpp"
#include "bw/rendering/scene/depth_hierarchy.hpp"
#include "bw/rendering/scene/draw_records.hpp"
#include "bw/rendering/scene/frame_views.hpp"
#include "bw/rendering/scene/instance_set.hpp"
#include "bw/rendering/scene/scene_geometry.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"
#include "depth_hierarchy_state.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Responsibility: the out-of-line part of bw/rendering/scene/culling.hpp.
//
// A phase is compute passes on the graph, one kernel each, every one
// sharing the CullingDispatch block (compute_shaders.hpp):
//   phase one: cull instances, scan instances, write survivors, cull
//   clusters, count records, write records;
//   phase two: cull clusters, count records, write records, over the main
//   view's work items phase one rejected by occlusion.
// An instance of the main view that passes its frustum and fails occlusion
// stays a survivor marked occluded, and those of its clusters in the
// frustum are rejected by occlusion, so phase two retests every instance
// and cluster phase one failed by occlusion (contract 4) without an
// instance pass of its own. Every position a kernel writes is a sum of
// counts in a fixed order: survivors and work items are placed by
// exclusive prefixes over groups of kGroupSize, per (view, pipeline)
// segment, so a list's records are in instance then level then cluster
// order with no atomic append (contract 6). The scratch layouts are
// bw/scene/culling.hlsli's.
//
// Every buffer the dispatch writes, the record and count buffers included,
// is the Culling's own, grown when a frame needs more and imported each
// frame, so each has one storage view made when it is made: nine storage
// slots in a steady frame. A buffer remade on growth releases its old
// view through the device's deferred delete, so a frame that grows every
// buffer holds eighteen until the frames in flight complete. The views and
// lists the kernels read are made per frame as upload buffers with sampled
// views (no storage slot), released when the next frame replaces them.
namespace bw::scene {
namespace {

// The thread group of every kernel but the scan, and the alignment of
// each segment's first work item (bw/scene/culling.hlsli, BW_CULL_GROUP).
constexpr std::uint32_t kGroupSize = 64;
// Scratch entry sizes (bw/scene/culling.hlsli).
constexpr std::uint32_t kCandidateBytes = 16;
constexpr std::uint32_t kInstanceGroupBytes = 8;
constexpr std::uint32_t kSegmentBytes = 16;
constexpr std::uint32_t kSegmentTotalsBytes = 16;
constexpr std::uint32_t kClusterStateBytes = 4;
constexpr std::uint32_t kClusterGroupBytes = 4;
constexpr std::uint32_t kArgumentsBytes = 24;
constexpr std::uint64_t kPhaseTwoArguments = 12;
constexpr std::uint64_t kMinimumBufferBytes = 16;

enum class Kernel : std::uint8_t {
  CullInstances,
  ScanInstances,
  WriteSurvivors,
  CullClusters,
  CountRecords,
  WriteRecords,
};
constexpr std::size_t kKernelCount = 6;

constexpr std::array<std::string_view, kKernelCount> kKernelNames{
    kCullInstancesShader, kScanInstancesShader, kWriteSurvivorsShader,
    kCullClustersShader,  kCountRecordsShader,  kWriteRecordsShader};

// The scratch, record and count buffers, in the block's order.
enum class Owned : std::uint8_t {
  Candidates,
  InstanceGroups,
  Segments,
  Survivors,
  ClusterState,
  ClusterGroups,
  Arguments,
  Records,
  Counts,
};
constexpr std::size_t kOwnedCount = 9;

struct OwnedBuffer {
  rhi::Ref<rhi::Buffer> buffer{};
  rhi::Ref<rhi::ShaderResourceView> read{};
  rhi::Ref<rhi::UnorderedAccessView> write{};
  std::uint64_t size{};
  graph::BufferHandle handle{};
};

} // namespace

struct Culling::State {
  struct Pass {
    State* state{};
    Kernel kernel{Kernel::CullInstances};
    std::uint32_t phase{};
    void operator()(graph::RecordContext& context) const;
  };

  std::array<rhi::Ref<rhi::ComputePipeline>, kKernelCount> pipelines{};
  std::array<OwnedBuffer, kOwnedCount> owned{};
  rhi::Ref<rhi::Buffer> views{};
  rhi::Ref<rhi::ShaderResourceView> views_view{};
  rhi::Ref<rhi::Buffer> lists{};
  rhi::Ref<rhi::ShaderResourceView> lists_view{};
  // The frame's block, phase one's hierarchy, and the dispatch sizes.
  CullingDispatch block{};
  std::uint32_t instance_groups{};
  std::uint32_t views_count{};
  std::uint32_t pipelines_count{};
  const DepthHierarchy* hierarchy{};
  // The frame's handle of the instance records, which the instance,
  // survivor and cluster kernels read.
  graph::BufferHandle instances{};
  std::array<Pass, kKernelCount + 3> passes{};
};

struct CullingAccess {
  using State = Culling::State;
  [[nodiscard]] static Culling::State& state(Culling& culling) noexcept { return *culling.state_; }
};

namespace {

CullingFailure failed(CullingError error, std::string detail) {
  return CullingFailure{error, std::move(detail)};
}

OwnedBuffer& owned_buffer(CullingAccess::State& state, Owned which) {
  return state.owned[static_cast<std::size_t>(which)];
}

// Makes the buffer anew when it is smaller than `size`.
CullingFailure ensure(rhi::Device& device, OwnedBuffer& buffer, std::uint64_t size,
                      rhi::BufferUsage extra, std::string_view name) {
  size = std::max(size, kMinimumBufferBytes);
  if (buffer.buffer && buffer.size >= size) {
    return {};
  }
  rhi::BufferDescription description{};
  description.size = size;
  description.usage = rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::ShaderResource | extra;
  description.initial_access = rhi::Access::UnorderedAccessCompute;
  description.debug_name = name;
  rhi::BufferResult created = device.create_buffer(description);
  if (!created.ok()) {
    return failed(CullingError::BufferFailed,
                  std::string(name) + ": " + std::string(created.detail()));
  }
  rhi::ShaderResourceViewResult read =
      device.create_shader_resource_view(created.value(), rhi::BufferViewDescription{});
  if (!read.ok()) {
    return failed(CullingError::BufferFailed,
                  std::string(name) + ": " + std::string(read.detail()));
  }
  rhi::UnorderedAccessViewResult write =
      device.create_unordered_access_view(created.value(), rhi::BufferViewDescription{});
  if (!write.ok()) {
    return failed(CullingError::BufferFailed,
                  std::string(name) + ": " + std::string(write.detail()));
  }
  buffer.buffer = std::move(created.value());
  buffer.read = std::move(read.value());
  buffer.write = std::move(write.value());
  buffer.size = size;
  return {};
}

// A per-frame upload of `bytes` with a sampled view.
CullingFailure upload(rhi::Device& device, std::span<const std::byte> bytes, std::string_view name,
                      rhi::Ref<rhi::Buffer>& buffer, rhi::Ref<rhi::ShaderResourceView>& view) {
  std::vector<std::byte> padded(std::max<std::size_t>(bytes.size(), kMinimumBufferBytes));
  std::copy(bytes.begin(), bytes.end(), padded.begin());
  rhi::BufferDescription description{};
  description.size = padded.size();
  description.usage = rhi::BufferUsage::ShaderResource;
  description.initial_access = rhi::kShaderResourceMask;
  description.debug_name = name;
  rhi::BufferResult created = device.create_buffer(description, padded);
  if (!created.ok()) {
    return failed(CullingError::BufferFailed,
                  std::string(name) + ": " + std::string(created.detail()));
  }
  rhi::ShaderResourceViewResult viewed =
      device.create_shader_resource_view(created.value(), rhi::BufferViewDescription{});
  if (!viewed.ok()) {
    return failed(CullingError::BufferFailed,
                  std::string(name) + ": " + std::string(viewed.detail()));
  }
  buffer = std::move(created.value());
  view = std::move(viewed.value());
  return {};
}

CullingView culling_view(const SceneView& view) {
  CullingView out{};
  out.world_to_clip = shader_matrix(view.world_to_clip);
  for (std::size_t i = 0; i < kFrustumPlanes; ++i) {
    out.planes[i] =
        shaders::Float4{view.planes[i].a, view.planes[i].b, view.planes[i].c, view.planes[i].d};
  }
  out.detail_origin_older =
      shaders::Float3{view.detail_origins[0].x, view.detail_origins[0].y, view.detail_origins[0].z};
  out.detail_origin_newer =
      shaders::Float3{view.detail_origins[1].x, view.detail_origins[1].y, view.detail_origins[1].z};
  out.projection_scale = view.projection_scale;
  out.detail_transition = view.detail_transition;
  out.viewport_width = view.viewport_width;
  out.viewport_height = view.viewport_height;
  return out;
}

template <class T> std::span<const std::byte> bytes_of(const std::vector<T>& values) {
  return std::as_bytes(std::span<const T>(values));
}

std::uint32_t divide_up(std::uint64_t value, std::uint64_t divisor) {
  return static_cast<std::uint32_t>((value + divisor - 1U) / divisor);
}

} // namespace

void Culling::State::Pass::operator()(graph::RecordContext& context) const {
  rhi::CommandList& list = context.list();
  list.set_compute_pipeline(*state->pipelines[static_cast<std::size_t>(kernel)]);
  CullingDispatch block = state->block;
  block.phase = phase;
  if (phase == static_cast<std::uint32_t>(CullingPhase::Two)) {
    block.hierarchy = DepthHierarchyAccess::state(*state->hierarchy).views[1]->texture_index();
  }
  list.set_draw_block(block);
  switch (kernel) {
  case Kernel::CullInstances:
  case Kernel::WriteSurvivors:
    list.dispatch(rhi::DispatchArguments{state->instance_groups, state->views_count, 1});
    break;
  case Kernel::ScanInstances:
    list.dispatch(rhi::DispatchArguments{1, 1, 1});
    break;
  case Kernel::CountRecords: {
    const std::uint32_t lists =
        phase == 0U ? state->views_count * state->pipelines_count : state->pipelines_count;
    list.dispatch(rhi::DispatchArguments{divide_up(lists, kGroupSize), 1, 1});
    break;
  }
  case Kernel::CullClusters:
  case Kernel::WriteRecords:
    list.dispatch_indirect(*context.buffer(owned_buffer(*state, Owned::Arguments).handle),
                           phase == 0U ? 0U : kPhaseTwoArguments);
    break;
  }
}

Culling::Culling() : state_(std::make_unique<State>()) {}
Culling::Culling(Culling&&) noexcept = default;
Culling& Culling::operator=(Culling&&) noexcept = default;
Culling::~Culling() = default;

CullingFailure create_culling(rhi::Device& device, const std::filesystem::path& shader_directory,
                              Culling& out) {
  out = Culling{};
  Culling built;
  CullingAccess::State& state = CullingAccess::state(built);
  for (std::size_t k = 0; k < kKernelCount; ++k) {
    shaders::ArtifactFile artifact;
    const shaders::ArtifactFileFailure read = shaders::read_artifact_file(
        shader_directory, kKernelNames[k], shaders::PermutationId{0}, artifact);
    if (!read.ok()) {
      return failed(CullingError::ArtifactFailed,
                    shaders::artifact_file_failure_text(read, kKernelNames[k]));
    }
    rhi::ComputePipelineResult pipeline = device.compute_pipeline(
        rhi::ComputePipelineDescription{&artifact.artifact, kKernelNames[k]});
    if (!pipeline.ok()) {
      return failed(CullingError::PipelineFailed,
                    std::string(kKernelNames[k]) + ": " + std::string(pipeline.detail()));
    }
    state.pipelines[k] = std::move(pipeline.value());
  }
  out = std::move(built);
  return {};
}

// Every view is tested by one dispatch, frustum then the previous
// hierarchy.
CullingFailure declare_culling_phase_one(Culling& culling, rhi::Device& device,
                                         graph::FrameGraph& graph, const CullingInputs& inputs,
                                         CullingFrame& frame) {
  CullingAccess::State& state = CullingAccess::state(culling);
  const std::span<const SceneView> views = inputs.views->views();
  const auto view_count = static_cast<std::uint32_t>(views.size());
  const std::uint32_t slots = inputs.instances->extent();
  // The pipelines a frame draws: up to the highest index any live instance
  // has, so a list exists for every index below it.
  std::uint32_t pipeline_count = 0;
  for (std::uint32_t p = 0; p < kMaxPipelines; ++p) {
    if (inputs.instances->cluster_capacity(p) != 0U) {
      pipeline_count = p + 1U;
    }
  }
  std::vector<std::uint32_t> capacities(pipeline_count);
  for (std::uint32_t p = 0; p < pipeline_count; ++p) {
    capacities[p] = inputs.instances->cluster_capacity(p);
  }
  const DrawListLayout layout = make_draw_list_layout(view_count, capacities);
  for (const std::uint32_t capacity : capacities) {
    if (capacity > device.capabilities().max_multi_draw_count) {
      return failed(CullingError::ListTooLarge,
                    "a list of " + std::to_string(capacity) + " records exceeds the device's " +
                        std::to_string(device.capabilities().max_multi_draw_count));
    }
  }

  const std::uint32_t groups = divide_up(slots, kGroupSize);
  const std::uint64_t segments = std::uint64_t{view_count} * pipeline_count;
  // Each view's work items are its instances' clusters plus up to a group
  // of alignment per segment (culling.hpp, contract 6: nothing overflows).
  const std::uint64_t work =
      std::uint64_t{view_count} *
      (inputs.instances->total_cluster_capacity() + (std::uint64_t{kGroupSize} * pipeline_count));
  const std::array<std::pair<std::uint64_t, rhi::BufferUsage>, kOwnedCount> sizes{{
      {std::uint64_t{view_count} * slots * kCandidateBytes, rhi::BufferUsage::None},
      {std::uint64_t{view_count} * groups * pipeline_count * kInstanceGroupBytes,
       rhi::BufferUsage::None},
      {(segments * kSegmentBytes) + kSegmentTotalsBytes, rhi::BufferUsage::None},
      {std::uint64_t{view_count} * slots * sizeof(CullSurvivor), rhi::BufferUsage::None},
      {work * kClusterStateBytes, rhi::BufferUsage::None},
      {(work / kGroupSize + 1U) * kClusterGroupBytes, rhi::BufferUsage::None},
      {kArgumentsBytes, rhi::BufferUsage::IndirectArguments},
      {layout.record_bytes, rhi::BufferUsage::IndirectArguments},
      {layout.count_bytes, rhi::BufferUsage::IndirectArguments},
  }};
  constexpr std::array<std::string_view, kOwnedCount> kNames{
      "culling candidates", "culling instance groups", "culling segments",
      "culling survivors",  "culling cluster state",   "culling cluster groups",
      "culling arguments",  "scene draw records",      "scene draw counts"};
  for (std::size_t b = 0; b < kOwnedCount; ++b) {
    CullingFailure made =
        ensure(device, state.owned[b], sizes[b].first, sizes[b].second, kNames[b]);
    if (!made.ok()) {
      return made;
    }
  }

  std::vector<CullingView> view_records;
  view_records.reserve(view_count);
  for (const SceneView& view : views) {
    view_records.push_back(culling_view(view));
  }
  std::vector<CullingList> list_records;
  list_records.reserve(segments + pipeline_count);
  const auto list_record = [](const DrawList& list) {
    return CullingList{static_cast<std::uint32_t>(list.record_offset / kDrawRecordBytes),
                       static_cast<std::uint32_t>(list.count_offset / sizeof(std::uint32_t)),
                       list.capacity, 0};
  };
  for (std::uint32_t v = 0; v < view_count; ++v) {
    for (std::uint32_t p = 0; p < pipeline_count; ++p) {
      list_records.push_back(list_record(layout.list(v, p, DrawPhase::PhaseOne)));
    }
  }
  for (std::uint32_t p = 0; p < pipeline_count; ++p) {
    list_records.push_back(list_record(layout.list(kMainView, p, DrawPhase::PhaseTwo)));
  }
  CullingFailure uploaded =
      upload(device, bytes_of(view_records), "culling views", state.views, state.views_view);
  if (!uploaded.ok()) {
    return uploaded;
  }
  uploaded = upload(device, bytes_of(list_records), "culling lists", state.lists, state.lists_view);
  if (!uploaded.ok()) {
    return uploaded;
  }

  for (OwnedBuffer& buffer : state.owned) {
    buffer.handle = graph.import_buffer(graph::ImportedBuffer{buffer.buffer.get(),
                                                              rhi::Access::UnorderedAccessCompute,
                                                              rhi::Access::UnorderedAccessCompute});
  }
  frame.records = owned_buffer(state, Owned::Records).handle;
  frame.counts = owned_buffer(state, Owned::Counts).handle;
  frame.records_index = owned_buffer(state, Owned::Records).read->buffer_index();
  frame.layout = layout;

  const DepthHierarchyAccess::State& hierarchy = DepthHierarchyAccess::state(*inputs.hierarchy);
  const bool history_usable = inputs.history.valid() && inputs.hierarchy->has_history() &&
                              inputs.views->previous_main() != nullptr;
  CullingDispatch& block = state.block;
  block = CullingDispatch{};
  if (history_usable) {
    block.previous_world_to_clip = shader_matrix(inputs.views->previous_main()->world_to_clip);
    block.hierarchy = hierarchy.views[0]->texture_index();
    block.flags = kCullingHistoryUsable;
  }
  block.instances = inputs.instance_buffer->view->buffer_index();
  block.levels = inputs.geometry_buffers->levels_view->buffer_index();
  block.clusters = inputs.geometry_buffers->clusters_view->buffer_index();
  block.views = state.views_view->buffer_index();
  block.lists = state.lists_view->buffer_index();
  block.candidates_read = owned_buffer(state, Owned::Candidates).read->buffer_index();
  block.instance_groups_read = owned_buffer(state, Owned::InstanceGroups).read->buffer_index();
  block.segments_read = owned_buffer(state, Owned::Segments).read->buffer_index();
  block.survivors_read = owned_buffer(state, Owned::Survivors).read->buffer_index();
  block.cluster_state_read = owned_buffer(state, Owned::ClusterState).read->buffer_index();
  block.cluster_groups_read = owned_buffer(state, Owned::ClusterGroups).read->buffer_index();
  block.candidates = owned_buffer(state, Owned::Candidates).write->storage_buffer_index();
  block.instance_groups = owned_buffer(state, Owned::InstanceGroups).write->storage_buffer_index();
  block.segments = owned_buffer(state, Owned::Segments).write->storage_buffer_index();
  block.survivors = owned_buffer(state, Owned::Survivors).write->storage_buffer_index();
  block.cluster_state = owned_buffer(state, Owned::ClusterState).write->storage_buffer_index();
  block.cluster_groups = owned_buffer(state, Owned::ClusterGroups).write->storage_buffer_index();
  block.arguments = owned_buffer(state, Owned::Arguments).write->storage_buffer_index();
  block.records = owned_buffer(state, Owned::Records).write->storage_buffer_index();
  block.counts = owned_buffer(state, Owned::Counts).write->storage_buffer_index();
  block.instance_capacity = slots;
  block.view_count = view_count;
  block.pipeline_count = pipeline_count;
  block.hierarchy_width = hierarchy.extent.width;
  block.hierarchy_height = hierarchy.extent.height;
  block.hierarchy_levels = hierarchy.levels;
  state.instance_groups = groups;
  state.views_count = view_count;
  state.pipelines_count = pipeline_count;
  state.hierarchy = inputs.hierarchy;
  state.instances = inputs.instance_buffer->handle;
  if (pipeline_count == 0U || slots == 0U) {
    // No instance draws through a list: no list exists and nothing runs.
    return {};
  }

  const auto use = [&](Owned which, graph::Access access) {
    return graph::BufferUse{owned_buffer(state, which).handle, access};
  };
  constexpr graph::Access kRead = graph::Access::Sampled;
  constexpr graph::Access kWrite = graph::Access::ComputeWrite;
  std::vector<graph::TextureUse> history;
  if (history_usable) {
    history.push_back(graph::TextureUse{inputs.history, kRead, graph::Contents::Keep, 0});
  }
  state.passes[0] = CullingAccess::State::Pass{&state, Kernel::CullInstances, 0};
  state.passes[1] = CullingAccess::State::Pass{&state, Kernel::ScanInstances, 0};
  state.passes[2] = CullingAccess::State::Pass{&state, Kernel::WriteSurvivors, 0};
  state.passes[3] = CullingAccess::State::Pass{&state, Kernel::CullClusters, 0};
  state.passes[4] = CullingAccess::State::Pass{&state, Kernel::CountRecords, 0};
  state.passes[5] = CullingAccess::State::Pass{&state, Kernel::WriteRecords, 0};
  const graph::BufferUse instances{state.instances, kRead};
  const std::array<graph::BufferUse, 3> cull_instances{instances, use(Owned::Candidates, kWrite),
                                                       use(Owned::InstanceGroups, kWrite)};
  graph.add_pass(graph::PassDeclaration{"culling one: instances", graph::PassKind::Compute, history,
                                        cull_instances, state.passes[0]});
  const std::array<graph::BufferUse, 3> scan{use(Owned::InstanceGroups, kWrite),
                                             use(Owned::Segments, kWrite),
                                             use(Owned::Arguments, kWrite)};
  graph.add_pass(graph::PassDeclaration{
      "culling one: scan", graph::PassKind::Compute, {}, scan, state.passes[1]});
  const std::array<graph::BufferUse, 5> survivors{
      instances, use(Owned::Candidates, kRead), use(Owned::InstanceGroups, kRead),
      use(Owned::Segments, kRead), use(Owned::Survivors, kWrite)};
  graph.add_pass(graph::PassDeclaration{
      "culling one: survivors", graph::PassKind::Compute, {}, survivors, state.passes[2]});
  const std::array<graph::BufferUse, 6> clusters{instances,
                                                 use(Owned::Arguments, graph::Access::Arguments),
                                                 use(Owned::Segments, kRead),
                                                 use(Owned::Survivors, kRead),
                                                 use(Owned::ClusterState, kWrite),
                                                 use(Owned::ClusterGroups, kWrite)};
  graph.add_pass(graph::PassDeclaration{"culling one: clusters", graph::PassKind::Compute, history,
                                        clusters, state.passes[3]});
  const std::array<graph::BufferUse, 3> count{
      use(Owned::Segments, kRead), use(Owned::ClusterGroups, kWrite), use(Owned::Counts, kWrite)};
  graph.add_pass(graph::PassDeclaration{
      "culling one: counts", graph::PassKind::Compute, {}, count, state.passes[4]});
  const std::array<graph::BufferUse, 6> records{use(Owned::Arguments, graph::Access::Arguments),
                                                use(Owned::Segments, kRead),
                                                use(Owned::Survivors, kRead),
                                                use(Owned::ClusterState, kRead),
                                                use(Owned::ClusterGroups, kRead),
                                                use(Owned::Records, kWrite)};
  graph.add_pass(graph::PassDeclaration{
      "culling one: records", graph::PassKind::Compute, {}, records, state.passes[5]});
  return {};
}

// What phase one culled is retested against the refreshed hierarchy and
// drawn the same frame.
void declare_culling_phase_two(Culling& culling, graph::FrameGraph& graph,
                               const CullingFrame& frame, graph::TextureHandle first_build) {
  CullingAccess::State& state = CullingAccess::state(culling);
  if (frame.layout.pipeline_count == 0U || state.block.instance_capacity == 0U) {
    return;
  }
  const auto use = [&](Owned which, graph::Access access) {
    return graph::BufferUse{owned_buffer(state, which).handle, access};
  };
  constexpr graph::Access kRead = graph::Access::Sampled;
  constexpr graph::Access kWrite = graph::Access::ComputeWrite;
  constexpr auto kTwo = static_cast<std::uint32_t>(CullingPhase::Two);
  state.passes[6] = CullingAccess::State::Pass{&state, Kernel::CullClusters, kTwo};
  state.passes[7] = CullingAccess::State::Pass{&state, Kernel::CountRecords, kTwo};
  state.passes[8] = CullingAccess::State::Pass{&state, Kernel::WriteRecords, kTwo};
  const std::array<graph::TextureUse, 1> hierarchy{
      graph::TextureUse{first_build, kRead, graph::Contents::Keep, 0}};
  const std::array<graph::BufferUse, 6> clusters{graph::BufferUse{state.instances, kRead},
                                                 use(Owned::Arguments, graph::Access::Arguments),
                                                 use(Owned::Segments, kRead),
                                                 use(Owned::Survivors, kRead),
                                                 use(Owned::ClusterState, kWrite),
                                                 use(Owned::ClusterGroups, kWrite)};
  graph.add_pass(graph::PassDeclaration{"culling two: clusters", graph::PassKind::Compute,
                                        hierarchy, clusters, state.passes[6]});
  const std::array<graph::BufferUse, 3> count{
      use(Owned::Segments, kRead), use(Owned::ClusterGroups, kWrite), use(Owned::Counts, kWrite)};
  graph.add_pass(graph::PassDeclaration{
      "culling two: counts", graph::PassKind::Compute, {}, count, state.passes[7]});
  const std::array<graph::BufferUse, 6> records{use(Owned::Arguments, graph::Access::Arguments),
                                                use(Owned::Segments, kRead),
                                                use(Owned::Survivors, kRead),
                                                use(Owned::ClusterState, kRead),
                                                use(Owned::ClusterGroups, kRead),
                                                use(Owned::Records, kWrite)};
  graph.add_pass(graph::PassDeclaration{
      "culling two: records", graph::PassKind::Compute, {}, records, state.passes[8]});
}

} // namespace bw::scene
