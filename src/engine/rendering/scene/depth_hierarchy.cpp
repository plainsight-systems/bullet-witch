#include "bw/rendering/scene/depth_hierarchy.hpp"

#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/scene/compute_shaders.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"
#include "depth_hierarchy_state.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

// Responsibility: the out-of-line part of bw/rendering/scene/depth_hierarchy.hpp.
//
// The build is two passes per build: a compute pass that reduces level by
// level into a raw scratch buffer, one dispatch per level, each reading the
// level the dispatch before it wrote; and a copy pass that copies each level into
// the build's texture (compute_shaders.hpp says why). The
// scratch buffer is shared by both builds and by every level: one storage
// slot. A level's rows are kRowAlignment-byte multiples from a
// kLevelAlignment-byte start, the alignments a buffer-to-texture copy
// needs on Direct3D 12, kept on every target so the layout is one.
//
// The dispatches of one pass are ordered by the backend: the Metal backend
// records a pass's dispatches on one serial compute encoder, where each
// dispatch completes before the next begins. The RHI has no transition from
// storage to the same storage (transitions.hpp reports it as redundant), so
// a backend whose API needs a barrier between dependent dispatches inserts
// it itself; the Direct3D 12 backend, not yet written, must.
//
// The reduce pass makes a sampled view of the frame's depth texture while
// it records, since that texture exists only while the frame executes; the
// view is released after recording and the device keeps it until the
// frame completes. A refused view records nothing, neither the reduce nor
// that build's copy, and is returned by the next begin_frame as ViewFailed,
// so the failure is visible.
// Level 0 is half the power-of-two-rounded view size, and its texel i
// reads the depth two by two from pixel 2i.
namespace bw::scene {
namespace {

// Thread group of depth_hierarchy_reduce.hlsl: words of two texels by rows.
constexpr std::uint32_t kReduceGroupWidth = 8;
constexpr std::uint32_t kReduceGroupHeight = 8;
constexpr std::uint32_t kRowAlignment = 256;
constexpr std::uint32_t kLevelAlignment = 512;
constexpr std::size_t kHistory = 0;
constexpr std::size_t kFirstBuild = 1;

std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment) noexcept {
  return (value + alignment - 1U) / alignment * alignment;
}

DepthHierarchyFailure failed(DepthHierarchyError error, std::string detail) {
  return DepthHierarchyFailure{error, std::move(detail)};
}

// A failed begin_frame leaves nothing usable: no texture, no scratch, and a
// zero extent and level count (depth_hierarchy.hpp, contract 4).
DepthHierarchyFailure failed_frame(DepthHierarchyAccess::State& state, DepthHierarchyError error,
                                   std::string detail) {
  state.has_history = false;
  state.second_declared = false;
  state.textures = {};
  state.views = {};
  state.scratch = {};
  state.scratch_view = {};
  state.view = {};
  state.extent = {};
  state.levels = 0;
  state.layout.clear();
  return failed(error, std::move(detail));
}

} // namespace

rhi::Extent2 depth_hierarchy_extent(rhi::Extent2 view) noexcept {
  const auto axis = [](std::uint32_t size) {
    return std::max<std::uint32_t>(std::bit_ceil(std::max<std::uint32_t>(size, 1U)) / 2U, 1U);
  };
  return rhi::Extent2{axis(view.width), axis(view.height)};
}

std::uint32_t depth_hierarchy_levels(rhi::Extent2 extent) noexcept {
  const std::uint32_t largest = std::max<std::uint32_t>(std::max(extent.width, extent.height), 1U);
  return static_cast<std::uint32_t>(std::bit_width(largest));
}

DepthHierarchy::DepthHierarchy() : state_(std::make_unique<State>()) {}
DepthHierarchy::DepthHierarchy(DepthHierarchy&&) noexcept = default;
DepthHierarchy& DepthHierarchy::operator=(DepthHierarchy&&) noexcept = default;
DepthHierarchy::~DepthHierarchy() = default;

DepthHierarchyFailure DepthHierarchy::begin_frame(rhi::Device& device, rhi::Extent2 view) {
  State& state = *state_;
  state.device = &device;
  if (!state.view_failure.empty()) {
    std::string detail = std::move(state.view_failure);
    state.view_failure.clear();
    return failed_frame(state, DepthHierarchyError::ViewFailed, std::move(detail));
  }
  if (view == state.view && state.textures[kHistory]) {
    state.has_history = state.second_declared;
    state.second_declared = false;
    return {};
  }
  state.has_history = false;
  state.second_declared = false;
  state.textures = {};
  state.views = {};
  state.scratch = {};
  state.scratch_view = {};
  state.view = {};
  state.extent = depth_hierarchy_extent(view);
  state.levels = depth_hierarchy_levels(state.extent);
  state.layout.clear();
  std::uint32_t offset = 0;
  for (std::uint32_t level = 0; level < state.levels; ++level) {
    const rhi::Extent2 extent{std::max(state.extent.width >> level, 1U),
                              std::max(state.extent.height >> level, 1U)};
    const std::uint32_t pitch = align_up(extent.width * 2U, kRowAlignment);
    state.layout.push_back(DepthHierarchyLevel{extent, offset, pitch});
    offset = align_up(offset + (pitch * extent.height), kLevelAlignment);
  }
  for (std::size_t t = 0; t < state.textures.size(); ++t) {
    rhi::TextureDescription description{};
    description.extent = rhi::Extent3{state.extent.width, state.extent.height, 1};
    description.mip_levels = static_cast<std::uint8_t>(state.levels);
    description.format = kDepthHierarchyFormat;
    description.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::CopyDestination;
    description.initial_access = rhi::kShaderResourceMask;
    description.debug_name = t == kHistory ? "depth hierarchy history" : "depth hierarchy first";
    rhi::TextureResult texture = device.create_texture(description);
    if (!texture.ok()) {
      return failed_frame(state, DepthHierarchyError::TextureFailed, std::string(texture.detail()));
    }
    rhi::ShaderResourceViewResult viewed =
        device.create_shader_resource_view(texture.value(), rhi::TextureViewDescription{});
    if (!viewed.ok()) {
      return failed_frame(state, DepthHierarchyError::ViewFailed, std::string(viewed.detail()));
    }
    state.textures[t] = std::move(texture.value());
    state.views[t] = std::move(viewed.value());
  }
  rhi::BufferDescription scratch{};
  scratch.size = std::max<std::uint32_t>(offset, 16U);
  scratch.usage = rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopySource;
  scratch.initial_access = rhi::Access::UnorderedAccessCompute;
  scratch.debug_name = "depth hierarchy levels";
  rhi::BufferResult buffer = device.create_buffer(scratch);
  if (!buffer.ok()) {
    return failed_frame(state, DepthHierarchyError::BufferFailed, std::string(buffer.detail()));
  }
  rhi::UnorderedAccessViewResult storage =
      device.create_unordered_access_view(buffer.value(), rhi::BufferViewDescription{});
  if (!storage.ok()) {
    return failed_frame(state, DepthHierarchyError::BufferFailed, std::string(storage.detail()));
  }
  state.scratch = std::move(buffer.value());
  state.scratch_view = std::move(storage.value());
  state.view = view;
  return {};
}

bool DepthHierarchy::has_history() const noexcept { return state_->has_history; }

void DepthHierarchy::invalidate() noexcept {
  state_->has_history = false;
  state_->second_declared = false;
}

rhi::Extent2 DepthHierarchy::extent() const noexcept { return state_->extent; }
std::uint32_t DepthHierarchy::levels() const noexcept { return state_->levels; }
rhi::Texture* DepthHierarchy::history() const noexcept { return state_->textures[kHistory].get(); }
rhi::Texture* DepthHierarchy::first_build() const noexcept {
  return state_->textures[kFirstBuild].get();
}

DepthHierarchyFailure create_depth_hierarchy(rhi::Device& device,
                                             const std::filesystem::path& shader_directory,
                                             DepthHierarchy& out) {
  out = DepthHierarchy{};
  shaders::ArtifactFile artifact;
  const shaders::ArtifactFileFailure read = shaders::read_artifact_file(
      shader_directory, kDepthHierarchyReduceShader, shaders::PermutationId{0}, artifact);
  if (!read.ok()) {
    return failed(DepthHierarchyError::ArtifactFailed,
                  shaders::artifact_file_failure_text(read, kDepthHierarchyReduceShader));
  }
  rhi::ComputePipelineResult pipeline = device.compute_pipeline(
      rhi::ComputePipelineDescription{&artifact.artifact, "depth hierarchy reduce"});
  if (!pipeline.ok()) {
    return failed(DepthHierarchyError::PipelineFailed, std::string(pipeline.detail()));
  }
  DepthHierarchy built;
  DepthHierarchyAccess::state(built).device = &device;
  DepthHierarchyAccess::state(built).reduce = std::move(pipeline.value());
  out = std::move(built);
  return {};
}

DepthHierarchyFrame declare_depth_hierarchy(DepthHierarchy& hierarchy, graph::FrameGraph& graph) {
  DepthHierarchyAccess::State& state = DepthHierarchyAccess::state(hierarchy);
  state.frame.history = graph.import_texture(graph::ImportedTexture{
      state.textures[kHistory].get(), rhi::kShaderResourceMask, rhi::kShaderResourceMask});
  state.frame.first_build = graph.import_texture(graph::ImportedTexture{
      state.textures[kFirstBuild].get(), rhi::kShaderResourceMask, rhi::kShaderResourceMask});
  state.scratch_handle = graph.import_buffer(
      graph::ImportedBuffer{state.scratch.get(), rhi::Access::UnorderedAccessCompute,
                            rhi::Access::UnorderedAccessCompute});
  graph.mark_history(state.frame.history);
  return state.frame;
}

// Each level after level 0 is reduced from the level below.
void DepthHierarchy::State::Record::operator()(graph::RecordContext& context) const {
  rhi::CommandList& list = context.list();
  rhi::Buffer* scratch_buffer = context.buffer(state->scratch_handle);
  const auto index = static_cast<std::size_t>(build);
  if (copy) {
    // The reduce pass before it recorded nothing: the scratch holds another
    // build's levels, and the texture keeps what it held.
    if (!state->reduced[index]) {
      return;
    }
    rhi::Texture* texture = context.texture(
        build == DepthHierarchyBuild::First ? state->frame.first_build : state->frame.history);
    for (std::uint32_t level = 0; level < state->levels; ++level) {
      const DepthHierarchyLevel& placed = state->layout[level];
      list.copy_buffer_to_texture(*scratch_buffer,
                                  rhi::BufferTextureRegion{placed.offset, placed.pitch,
                                                           placed.pitch * placed.extent.height,
                                                           static_cast<std::uint8_t>(level), 0},
                                  *texture);
    }
    return;
  }
  state->reduced[index] = false;
  rhi::Texture* depth = context.texture(state->depth[index]);
  rhi::TextureViewDescription plane{};
  plane.plane = rhi::TexturePlane::Depth;
  rhi::ShaderResourceViewResult viewed =
      state->device->create_shader_resource_view(rhi::Ref<rhi::Texture>(depth), plane);
  if (!viewed.ok()) {
    state->view_failure = "the depth texture's sampled view: " + std::string(viewed.detail());
    return;
  }
  list.set_compute_pipeline(*state->reduce);
  for (std::uint32_t level = 0; level < state->levels; ++level) {
    const DepthHierarchyLevel& placed = state->layout[level];
    DepthHierarchyReduce block{};
    block.depth = viewed.value()->texture_index();
    block.levels = state->scratch_view->storage_buffer_index();
    block.level = level;
    if (level == 0U) {
      block.source_width = state->view.width;
      block.source_height = state->view.height;
    } else {
      const DepthHierarchyLevel& below = state->layout[level - 1U];
      block.source_offset = below.offset;
      block.source_pitch = below.pitch;
      block.source_width = below.extent.width;
      block.source_height = below.extent.height;
    }
    block.destination_offset = placed.offset;
    block.destination_pitch = placed.pitch;
    block.destination_width = placed.extent.width;
    block.destination_height = placed.extent.height;
    list.set_draw_block(block);
    const std::uint32_t words = (placed.extent.width + 1U) / 2U;
    list.dispatch(rhi::DispatchArguments{
        (words + kReduceGroupWidth - 1U) / kReduceGroupWidth,
        (placed.extent.height + kReduceGroupHeight - 1U) / kReduceGroupHeight, 1});
  }
  state->reduced[index] = true;
}

void declare_depth_hierarchy_build(DepthHierarchy& hierarchy, graph::FrameGraph& graph,
                                   const DepthHierarchyFrame& frame, graph::TextureHandle depth,
                                   DepthHierarchyBuild build) {
  DepthHierarchyAccess::State& state = DepthHierarchyAccess::state(hierarchy);
  const auto index = static_cast<std::size_t>(build);
  state.frame = frame;
  state.depth[index] = depth;
  state.records[index * 2U] = DepthHierarchyAccess::State::Record{&state, build, false};
  state.records[(index * 2U) + 1U] = DepthHierarchyAccess::State::Record{&state, build, true};
  const graph::TextureHandle target =
      build == DepthHierarchyBuild::First ? frame.first_build : frame.history;
  const std::array<graph::TextureUse, 1> reduce_textures{
      graph::TextureUse{depth, graph::Access::Sampled, graph::Contents::Keep, 0}};
  const std::array<graph::BufferUse, 1> reduce_buffers{
      graph::BufferUse{state.scratch_handle, graph::Access::ComputeWrite}};
  graph.add_pass(graph::PassDeclaration{
      build == DepthHierarchyBuild::First ? "depth hierarchy first reduce"
                                          : "depth hierarchy second reduce",
      graph::PassKind::Compute, reduce_textures, reduce_buffers, state.records[index * 2U]});
  const std::array<graph::TextureUse, 1> copy_textures{
      graph::TextureUse{target, graph::Access::CopyDestination, graph::Contents::Keep, 0}};
  const std::array<graph::BufferUse, 1> copy_buffers{
      graph::BufferUse{state.scratch_handle, graph::Access::CopySource}};
  graph.add_pass(graph::PassDeclaration{
      build == DepthHierarchyBuild::First ? "depth hierarchy first copy"
                                          : "depth hierarchy second copy",
      graph::PassKind::Copy, copy_textures, copy_buffers, state.records[(index * 2U) + 1U]});
  if (build == DepthHierarchyBuild::Second) {
    state.second_declared = true;
  }
}

} // namespace bw::scene
