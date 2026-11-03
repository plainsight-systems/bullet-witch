#include "metal_context.hpp"

#include "bw/memory/alignment.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/transitions.hpp"

#include "metal_device.hpp"
#include "metal_formats.hpp"
#include "metal_objects.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bw::rhi::metal {

namespace {

constexpr MTL::RenderStages kVertexAndFragment =
    static_cast<MTL::RenderStages>(MTL::RenderStageVertex | MTL::RenderStageFragment);

void write_address(std::span<std::byte> arguments, std::uint32_t offset, std::uint64_t address) {
  std::memcpy(arguments.data() + offset, &address, sizeof(address));
}

[[nodiscard]] Extent2 extent_at(const TextureDescription& d, std::uint32_t mip) noexcept {
  return Extent2{std::max(d.extent.width >> mip, 1U), std::max(d.extent.height >> mip, 1U)};
}

} // namespace

// The three table addresses do not change for the life of the device
// (metal_memory.hpp, DescriptorHeaps), so they are written once here.
MetalContext::MetalContext(MetalBackend& backend, const std::vector<MTL::Buffer*>& rings)
    : backend_(&backend) {
  rings_.reserve(rings.size());
  for (MTL::Buffer* ring : rings) {
    rings_.push_back(Ring{ring, 0, 0});
  }
  const BindingStructure& binding = backend.binding_structure();
  const DescriptorHeaps& heaps = backend.descriptor_heaps();
  write_address(arguments_, binding.table(RootParameterKind::ResourceTable).argument_offset,
                heaps.resource_table_address());
  write_address(arguments_, binding.table(RootParameterKind::SamplerTable).argument_offset,
                heaps.sampler_table_address());
  write_address(arguments_, binding.table(RootParameterKind::StorageTable).argument_offset,
                heaps.storage_table_address());
}

MetalContext::~MetalContext() {
  const AutoreleaseScope pool;
  end_encoder();
  if (command_buffer_ != nullptr) {
    command_buffer_->release();
  }
  for (const Ring& ring : rings_) {
    ring.buffer->release();
  }
}

// ---- Command buffer and encoders ------------------------------------------------

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalCommandQueue.cpp:106-130:
// CreateCommandBuffer under a scoped pool, with retained references off on
// Mac (:110-120), retained by its owner past the pool
// (MetalCommandBuffer.h:18-19). Followed: unretained references are safe
// because the deferred delete queue keeps every object a frame names alive
// until the frame completes (lifetime.hpp). Omitted: the encoder execution
// status error option (:121), a debugging aid.
MTL::CommandBuffer* MetalContext::command_buffer() {
  if (command_buffer_ == nullptr) {
    const AutoreleaseScope pool;
    const ScopedRelease<MTL::CommandBufferDescriptor> descriptor{
        MTL::CommandBufferDescriptor::alloc()->init()};
    descriptor->setRetainedReferences(false);
    command_buffer_ = backend_->queue().commandBuffer(descriptor.get());
    command_buffer_->retain();
#if BW_RHI_DEBUG
    set_label(*command_buffer_,
              "bw.rhi.metal.frame " + std::to_string(backend_->recording_frame()));
#endif
  }
  return command_buffer_;
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalCommandEncoder.cpp:617-845:
// EndEncoding ends whichever encoder is open and resets the per-encoder
// bindings. Followed. Omitted: the encoder fences (:680-695, :785, :808),
// which are null on Mac by default (MetalDevice.cpp:132-137, :343-346;
// MetalCommandQueue.cpp:146-170): tracked hazards order the encoders.
void MetalContext::end_encoder() {
  MTL::CommandEncoder* encoder = nullptr;
  switch (encoder_kind_) {
  case EncoderKind::Render:
    encoder = render_encoder_;
    render_encoder_ = nullptr;
    break;
  case EncoderKind::Blit:
    encoder = blit_encoder_;
    blit_encoder_ = nullptr;
    break;
  case EncoderKind::Compute:
    encoder = compute_encoder_;
    compute_encoder_ = nullptr;
    break;
  case EncoderKind::None:
    return;
  }
  // Markers pushed on this encoder close with it (end_marker then finds them
  // closed); they are on top of the stack, pushed after the encoder opened.
  for (auto it = markers_.rbegin(); it != markers_.rend(); ++it) {
    if (it->where != MarkerWhere::Encoder || it->encoder_serial != encoder_serial_) {
      break;
    }
    encoder->popDebugGroup();
    it->where = MarkerWhere::Closed;
  }
  encoder->endEncoding();
  encoder->release();
  encoder_kind_ = EncoderKind::None;
  heaps_resident_ = false;
  ring_resident_ = false;
  written_resident_ = false;
  graphics_pipeline_ = nullptr;
  for (; deferred_command_buffer_pops_ > 0U; --deferred_command_buffer_pops_) {
    command_buffer_->popDebugGroup();
  }
}

MTL::BlitCommandEncoder* MetalContext::blit_encoder() {
  if (encoder_kind_ == EncoderKind::Blit) {
    return blit_encoder_;
  }
  end_encoder();
  const AutoreleaseScope pool;
  blit_encoder_ = command_buffer()->blitCommandEncoder();
  blit_encoder_->retain();
  encoder_kind_ = EncoderKind::Blit;
  ++encoder_serial_;
  return blit_encoder_;
}

MTL::ComputeCommandEncoder* MetalContext::compute_encoder() {
  if (encoder_kind_ == EncoderKind::Compute) {
    return compute_encoder_;
  }
  end_encoder();
  const AutoreleaseScope pool;
  compute_encoder_ = command_buffer()->computeCommandEncoder();
  compute_encoder_->retain();
  encoder_kind_ = EncoderKind::Compute;
  ++encoder_serial_;
  const DescriptorHeaps& heaps = backend_->descriptor_heaps();
  compute_encoder_->setBuffer(heaps.resource_heap(), 0, kDescriptorHeapBindPoint);
  compute_encoder_->setBuffer(heaps.sampler_heap(), 0, kSamplerHeapBindPoint);
  return compute_encoder_;
}

MetalContext::Ring& MetalContext::frame_ring() noexcept {
  return rings_[backend_->recording_frame() % rings_.size()];
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalStateCache.cpp:1886-1926:
// FlushUsedResources declares the stage's heaps with UseHeaps and the
// resources it reaches indirectly with UseResources (Read or Read|Write);
// MetalCommandEncoder.cpp:1548-1620 forwards to useHeaps and useResources.
// Followed: every resource heap (buffers and textures reached through the
// descriptor heap or a block address), and, read-only, the frame's ring (the
// draw block address) and the two descriptor heaps, for both stages, once per
// encoder and again when the heap list changed. The descriptor heaps are
// declared although they are also bound at their bind points: with the
// tables in the root signature the converter's functions reach them through
// the table addresses in the argument buffer and mark the bind point
// arguments unused, and a buffer bound only at an unused argument is not
// resident (measured on the fixture draw: without the declaration the vertex
// function's texture handle read as null under Metal shader validation).
void MetalContext::declare_residency(MTL::RenderCommandEncoder& encoder) {
  ResourceMemory& memory = backend_->memory();
  if (!heaps_resident_ || memory.generation() != resident_heap_generation_) {
    resident_heap_generation_ = memory.heaps(heap_scratch_);
    if (!heap_scratch_.empty()) {
      encoder.useHeaps(heap_scratch_.data(), heap_scratch_.size(), kVertexAndFragment);
    }
    heaps_resident_ = true;
  }
  if (!ring_resident_) {
    const DescriptorHeaps& heaps = backend_->descriptor_heaps();
    const std::array<const MTL::Resource*, 3> read{frame_ring().buffer, heaps.resource_heap(),
                                                   heaps.sampler_heap()};
    encoder.useResources(read.data(), read.size(), MTL::ResourceUsageRead, kVertexAndFragment);
    ring_resident_ = true;
  }
}

void MetalContext::declare_residency(MTL::ComputeCommandEncoder& encoder) {
  ResourceMemory& memory = backend_->memory();
  if (!heaps_resident_ || memory.generation() != resident_heap_generation_) {
    resident_heap_generation_ = memory.heaps(heap_scratch_);
    if (!heap_scratch_.empty()) {
      encoder.useHeaps(heap_scratch_.data(), heap_scratch_.size());
    }
    heaps_resident_ = true;
  }
  if (!ring_resident_) {
    const DescriptorHeaps& heaps = backend_->descriptor_heaps();
    const std::array<const MTL::Resource*, 3> read{frame_ring().buffer, heaps.resource_heap(),
                                                   heaps.sampler_heap()};
    encoder.useResources(read.data(), read.size(), MTL::ResourceUsageRead);
    ring_resident_ = true;
  }
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalStateCache.cpp:1920-1922:
// FlushUsedResources' third branch declares the resources a stage writes with
// UseResources(Read|Write). Followed: every resource an unordered access view
// names (metal_memory.hpp, WritableResources), for both stages or the
// compute encoder, when the bound pipeline uses the storage table, once per
// encoder and again when the set changed. Compiled, not run: no artifact of
// this slice uses the storage table.
void MetalContext::declare_written(MTL::RenderCommandEncoder& encoder) {
  const WritableResources& writable = backend_->writable_resources();
  if (written_resident_ && writable.generation() == written_generation_) {
    return;
  }
  written_generation_ = writable.snapshot(written_scratch_);
  if (!written_scratch_.empty()) {
    encoder.useResources(
        written_scratch_.data(), written_scratch_.size(),
        static_cast<MTL::ResourceUsage>(MTL::ResourceUsageRead | MTL::ResourceUsageWrite),
        kVertexAndFragment);
  }
  written_resident_ = true;
}

void MetalContext::declare_written(MTL::ComputeCommandEncoder& encoder) {
  const WritableResources& writable = backend_->writable_resources();
  if (written_resident_ && writable.generation() == written_generation_) {
    return;
  }
  written_generation_ = writable.snapshot(written_scratch_);
  if (!written_scratch_.empty()) {
    encoder.useResources(
        written_scratch_.data(), written_scratch_.size(),
        static_cast<MTL::ResourceUsage>(MTL::ResourceUsageRead | MTL::ResourceUsageWrite));
  }
  written_resident_ = true;
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalStateCache.cpp:1928-1953:
// IRBindResourcesToEncoder flushes residency, binds the two descriptor heaps
// and sets the top-level argument buffer as bytes at
// kIRArgumentBufferBindPoint. Followed for both stages of a draw. The slice's
// plan carved the argument buffer from the frame's ring; set*Bytes is the
// pin's choice and is kept because draw has no error to return if a carve
// from the ring failed (backend.hpp, Context::draw), while set*Bytes cannot.
void MetalContext::bind_render_arguments() {
  declare_residency(*render_encoder_);
  if (graphics_pipeline_->binding().uses_storage_table) {
    declare_written(*render_encoder_);
  }
  render_encoder_->setVertexBytes(arguments_.data(), arguments_.size(), kArgumentBufferBindPoint);
  render_encoder_->setFragmentBytes(arguments_.data(), arguments_.size(), kArgumentBufferBindPoint);
}

// ---- Transitions ---------------------------------------------------------------

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalTransitionData.cpp:34-36:
// BeginResourceTransitions is empty. Followed: Metal tracks hazards on the
// heaps' resources (metal_memory.hpp), so a begin is no work.
void MetalContext::begin_transitions(const TransitionBatch& batch) { static_cast<void>(batch); }

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalTransitionData.cpp:38-120,
// 130-151: EndResourceTransitions inspects only whether the before or after
// access has an unordered access bit, and RHIEndTransitions then issues
// memoryBarrier(BarrierScopeBuffers | BarrierScopeTextures) on the compute
// encoder if one is open; with a render, blit or no encoder it issues
// nothing. Followed. What each access pair maps to on this backend:
//   - any pair with UnorderedAccessCompute or UnorderedAccessGraphics on
//     either side: a buffers-and-textures memory barrier when a compute
//     encoder is open, else no work (the next encoder is ordered after it by
//     tracked hazards);
//   - every other pair (render target, depth, shader resource, copy, vertex
//     and index, indirect, CPU read, Discard): no work; tracked hazards order
//     the encoders that write and read the resource.
// Every transition writes the resource's tracked access (transitions.hpp,
// apply_end): the after access on the graphics pipeline, the only one on a
// device without async compute (access.hpp, Pipeline).
// An after of Unknown is skipped and writes no tracked access: it is not a
// legal after (access.hpp), the validation layer names it (UnknownAfter),
// and the pin's checkf at MetalTransitionData.cpp:55 rejects it. An after of
// Discard writes the tracked access and is skipped before the unordered
// access test, as the pin skips it (:49-53, "Discard as a destination is a
// no-op").
void MetalContext::end_transitions(const TransitionBatch& batch) {
  bool unordered_access = false;
  for (const Transition& transition : batch.transitions) {
    if (transition.resource == nullptr || transition.after == Access::Unknown) {
      continue;
    }
    const Access before = transition.before == Access::Unknown
                              ? transition.resource->tracked_access().access
                              : transition.before;
    transition.resource->set_tracked_access(
        TrackedAccess{transition.after, effective_pipelines(batch.pipelines.destination, false)});
    if (transition.after == Access::Discard) {
      continue;
    }
    unordered_access = unordered_access || has_any(before, kUnorderedAccessMask) ||
                       has_any(transition.after, kUnorderedAccessMask);
  }
  if (unordered_access && encoder_kind_ == EncoderKind::Compute) {
    compute_encoder_->memoryBarrier(
        static_cast<MTL::BarrierScope>(MTL::BarrierScopeBuffers | MTL::BarrierScopeTextures));
  }
}

// ---- Render passes -------------------------------------------------------------

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalRHIContext.cpp:341-401:
// RHIBeginRenderPass starts the command buffer if needed, takes the pass's
// visibility buffer when it has occlusion queries, builds the render pass
// descriptor (MetalStateCache.cpp:440ff, load and store actions
// MetalRHIPrivate.h:176-185 and MetalStateCache.cpp:76-103), ends any open
// encoder and begins the render encoder, then sets the viewport to the whole
// of color target 0 (:390-400). Followed; the clear values are the
// textures' (resources.hpp, ClearValue). The scissor is reset to the whole
// target with the viewport. Departure, for the user to decide: the pin's
// StartRenderPass (MetalStateCache.cpp:2074-2111) rewrites the actions, a
// load that is not Clear becomes Load, a color Clear forces Store, and a
// writable depth or stencil forces Store; here the description's actions
// pass through unchanged. Omitted: the descriptor updates push (:345; the
// descriptor heaps are written at view creation) and the stage counter
// samples (no timestamp queries).
void MetalContext::begin_render_pass(const RenderPassDescription& pass) {
  const AutoreleaseScope pool;
  end_encoder();
  MTL::CommandBuffer* commands = command_buffer();
  const ScopedRelease<MTL::RenderPassDescriptor> descriptor{
      MTL::RenderPassDescriptor::alloc()->init()};
  Extent2 extent{1, 1};
  for (std::uint32_t i = 0; i < pass.color_count; ++i) {
    const ColorTarget& target = pass.colors[i];
    const auto& texture = static_cast<const MetalTexture&>(*target.texture);
    const TextureDescription& d = texture.description();
    MTL::RenderPassColorAttachmentDescriptor* attachment =
        descriptor->colorAttachments()->object(i);
    attachment->setTexture(texture.texture());
    attachment->setLevel(target.mip);
    if (d.dimension == TextureDimension::Texture3D) {
      attachment->setDepthPlane(target.slice);
    } else {
      attachment->setSlice(target.slice);
    }
    attachment->setLoadAction(load_action(target.load));
    attachment->setStoreAction(store_action(target.store));
    const auto& clear = d.clear_value.color;
    attachment->setClearColor(MTL::ClearColor::Make(clear[0], clear[1], clear[2], clear[3]));
    if (i == 0U) {
      extent = extent_at(d, target.mip);
    }
  }
  if (pass.depth_stencil.texture != nullptr) {
    const DepthStencilTarget& target = pass.depth_stencil;
    const auto& texture = static_cast<const MetalTexture&>(*target.texture);
    const TextureDescription& d = texture.description();
    MTL::RenderPassDepthAttachmentDescriptor* depth = descriptor->depthAttachment();
    depth->setTexture(texture.texture());
    depth->setLoadAction(load_action(target.depth_load));
    depth->setStoreAction(store_action(target.depth_store));
    depth->setClearDepth(d.clear_value.depth);
    if (format_info(d.format).has_stencil) {
      MTL::RenderPassStencilAttachmentDescriptor* stencil = descriptor->stencilAttachment();
      stencil->setTexture(texture.texture());
      stencil->setLoadAction(load_action(target.stencil_load));
      stencil->setStoreAction(store_action(target.stencil_store));
      stencil->setClearStencil(d.clear_value.stencil);
    }
    if (pass.color_count == 0U) {
      extent = extent_at(d, 0);
    }
  }
  if (pass.occlusion_queries != nullptr) {
    descriptor->setVisibilityResultBuffer(
        static_cast<const MetalQueryPool*>(pass.occlusion_queries)->results());
  }
  render_encoder_ = commands->renderCommandEncoder(descriptor.get());
  render_encoder_->retain();
  encoder_kind_ = EncoderKind::Render;
  ++encoder_serial_;
  set_label(*render_encoder_, pass.name);
  pass_width_ = extent.width;
  pass_height_ = extent.height;
  graphics_pipeline_ = nullptr;
  const DescriptorHeaps& heaps = backend_->descriptor_heaps();
  render_encoder_->setVertexBuffer(heaps.resource_heap(), 0, kDescriptorHeapBindPoint);
  render_encoder_->setVertexBuffer(heaps.sampler_heap(), 0, kSamplerHeapBindPoint);
  render_encoder_->setFragmentBuffer(heaps.resource_heap(), 0, kDescriptorHeapBindPoint);
  render_encoder_->setFragmentBuffer(heaps.sampler_heap(), 0, kSamplerHeapBindPoint);
  render_encoder_->setViewport(MTL::Viewport{0.0, 0.0, static_cast<double>(extent.width),
                                             static_cast<double>(extent.height), 0.0, 1.0});
  render_encoder_->setScissorRect(MTL::ScissorRect{0, 0, extent.width, extent.height});
}

// MetalRHIContext.cpp:403-423: RHIEndRenderPass ends the render encoder.
// Followed. Omitted: the resolve blits (:415-418), with resolves.
void MetalContext::end_render_pass() {
  if (encoder_kind_ == EncoderKind::Render) {
    end_encoder();
  }
}

// Placement violations (command_list.hpp, Render passes) are reported by
// the validation layer; without it the behavior is undefined, and here it
// is this: a command that needs the render encoder reaches none outside a
// pass, and a copy, fence or dispatch inside a pass is not recorded, so it
// cannot end the pass's encoder.

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalStateCache.cpp:2196-2264:
// SetRenderState sets the winding (counter-clockwise), cull mode, depth bias
// with clamp FLT_MAX, fill mode, depth-stencil state and depth clip mode from
// the bound states; :2477-2508 SetRenderPipelineState sets the pipeline.
// Followed at set_graphics_pipeline, where this RHI's pipeline carries its
// states.
void MetalContext::set_graphics_pipeline(GraphicsPipeline& pipeline) {
  if (render_encoder_ == nullptr) {
    return;
  }
  auto& metal_pipeline = static_cast<MetalGraphicsPipeline&>(pipeline);
  const GraphicsPipelineDescription& description = metal_pipeline.description();
  const RasterizerDescription& rasterizer = description.rasterizer->description();
  const auto* depth_stencil = static_cast<const MetalDepthStencilState*>(description.depth_stencil);
  render_encoder_->setRenderPipelineState(metal_pipeline.state());
  render_encoder_->setFrontFacingWinding(MTL::WindingCounterClockwise);
  render_encoder_->setCullMode(cull_mode(rasterizer.cull));
  render_encoder_->setDepthBias(rasterizer.depth_bias, rasterizer.slope_scale_depth_bias,
                                std::numeric_limits<float>::max());
  render_encoder_->setTriangleFillMode(fill_mode(rasterizer.fill));
  render_encoder_->setDepthClipMode(depth_clip_mode(rasterizer.depth_clip));
  render_encoder_->setDepthStencilState(depth_stencil->state());
  graphics_pipeline_ = &metal_pipeline;
  primitive_ = primitive_type(description.primitive);
}

void MetalContext::set_compute_pipeline(ComputePipeline& pipeline) {
  compute_pipeline_ = &static_cast<MetalComputePipeline&>(pipeline);
}

void MetalContext::set_viewport(const Viewport& viewport) {
  if (render_encoder_ == nullptr) {
    return;
  }
  render_encoder_->setViewport(MTL::Viewport{viewport.x, viewport.y, viewport.width,
                                             viewport.height, viewport.min_depth,
                                             viewport.max_depth});
}

// Metal requires the scissor inside the render targets; the rectangle is
// clamped to the pass's extent.
void MetalContext::set_scissor(const ScissorRect& rect) {
  if (render_encoder_ == nullptr) {
    return;
  }
  const auto clamp_to = [](std::int64_t value, std::uint32_t limit) {
    return static_cast<NS::UInteger>(std::clamp<std::int64_t>(value, 0, limit));
  };
  const NS::UInteger x0 = clamp_to(rect.x, pass_width_);
  const NS::UInteger y0 = clamp_to(rect.y, pass_height_);
  const NS::UInteger x1 = clamp_to(static_cast<std::int64_t>(rect.x) + rect.width, pass_width_);
  const NS::UInteger y1 = clamp_to(static_cast<std::int64_t>(rect.y) + rect.height, pass_height_);
  render_encoder_->setScissorRect(MTL::ScissorRect{x0, y0, x1 - x0, y1 - y0});
}

void MetalContext::set_stencil_reference(std::uint32_t reference) {
  if (render_encoder_ == nullptr) {
    return;
  }
  render_encoder_->setStencilReferenceValue(reference);
}

// No pipeline of this backend reads a vertex buffer: a non-empty vertex
// layout is VertexLayoutUnsupported at creation (pipeline_states.hpp), so a
// bound vertex buffer has no reader and nothing is recorded. Omitted for
// that reason: Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalStateCache.cpp:1955-1968
// IRMapVertexBuffers, which arrives with the stage-in synthesis.
void MetalContext::set_vertex_buffer(std::uint32_t slot, Buffer& buffer, std::uint64_t offset) {
  static_cast<void>(slot);
  static_cast<void>(buffer);
  static_cast<void>(offset);
}

void MetalContext::set_index_buffer(Buffer& buffer, IndexFormat format, std::uint64_t offset) {
  index_buffer_ = static_cast<MetalBuffer&>(buffer).buffer();
  index_format_ = format;
  index_offset_ = offset;
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalStateCache.cpp:1626,
// :1629-1636: a uniform buffer's GPU address goes into its CBV table entry.
// Followed at the static slot's offset of the binding structure.
void MetalContext::set_static_block(std::uint32_t slot, Buffer& buffer, std::uint64_t offset) {
  if (slot >= shaders::kMaxStaticBlocks) {
    return;
  }
  const std::uint64_t address = static_cast<MetalBuffer&>(buffer).buffer()->gpuAddress() + offset;
  write_address(arguments_, backend_->binding_structure().static_block(slot).argument_offset,
                address);
}

// command_list.hpp, Blocks, and invariant 7: the bytes go to the frame's ring
// at a BufferOffsetAlignment offset and the draw block parameter takes that
// address; the ring never grows.
ExecuteError MetalContext::set_draw_block(std::span<const std::byte> bytes,
                                          shaders::LayoutHash layout) {
  static_cast<void>(layout);
  Ring& ring = frame_ring();
  const std::uint64_t capacity = ring.buffer->length();
  if (bytes.size() > capacity) {
    return ExecuteError::DrawBlockTooLarge;
  }
  const std::uint64_t offset =
      bw::memory::align_up(ring.offset, std::uint64_t{kBufferOffsetAlignment});
  if (offset > capacity || bytes.size() > capacity - offset) {
    return ExecuteError::TransientBudgetExhausted;
  }
  auto* base = static_cast<std::byte*>(ring.buffer->contents());
  if (!bytes.empty()) {
    std::memcpy(base + offset, bytes.data(), bytes.size());
  }
  ring.offset = offset + bytes.size();
  ring.last_frame = backend_->recording_frame();
  write_address(arguments_, backend_->binding_structure().draw_block().argument_offset,
                ring.buffer->gpuAddress() + offset);
  return ExecuteError::None;
}

void MetalContext::set_root_constants(std::uint32_t first_word,
                                      std::span<const std::uint32_t> words) {
  const RootParameter& constants = backend_->binding_structure().root_constants();
  const std::uint32_t capacity = constants.argument_size / kRootConstantWordBytes;
  if (first_word >= capacity) {
    return;
  }
  const std::size_t count = std::min<std::size_t>(words.size(), capacity - first_word);
  const std::size_t offset = static_cast<std::size_t>(constants.argument_offset) +
                             static_cast<std::size_t>(first_word) * kRootConstantWordBytes;
  std::memcpy(arguments_.data() + offset, words.data(), count * sizeof(std::uint32_t));
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalCommands.cpp:1036-1048:
// RHIDrawPrimitive prepares the render state and, under the converter, draws
// with IRRuntimeDrawPrimitives (which sets the draw arguments and the
// non-indexed marker at the converter's bind points). Followed.
void MetalContext::draw(const DrawArguments& arguments) {
  if (render_encoder_ == nullptr || graphics_pipeline_ == nullptr) {
    return;
  }
  bind_render_arguments();
  IRRuntimeDrawPrimitives(render_encoder_, primitive_, arguments.first_vertex,
                          arguments.vertex_count, arguments.instance_count,
                          arguments.first_instance);
}

// MetalCommands.cpp:1082 (indexed): IRRuntimeDrawIndexedPrimitives with the
// bound index buffer. Followed; the first index becomes the byte offset into
// the index buffer.
void MetalContext::draw_indexed(const DrawIndexedArguments& arguments) {
  if (render_encoder_ == nullptr || graphics_pipeline_ == nullptr || index_buffer_ == nullptr) {
    return;
  }
  bind_render_arguments();
  const std::uint64_t offset = index_offset_ + static_cast<std::uint64_t>(arguments.first_index) *
                                                   index_bytes(index_format_);
  IRRuntimeDrawIndexedPrimitives(
      render_encoder_, primitive_, arguments.index_count, index_type(index_format_), index_buffer_,
      offset, arguments.instance_count, arguments.base_vertex, arguments.first_instance);
}

// The converter's compute function reads the same top-level argument buffer
// and heaps at the same bind points; the thread group size is the shader's
// (artifact.hpp, Reflection::thread_group_size).
void MetalContext::dispatch(const DispatchArguments& arguments) {
  if (compute_pipeline_ == nullptr || render_encoder_ != nullptr) {
    return;
  }
  MTL::ComputeCommandEncoder* encoder = compute_encoder();
  encoder->setComputePipelineState(compute_pipeline_->state());
  declare_residency(*encoder);
  if (compute_pipeline_->binding().uses_storage_table) {
    declare_written(*encoder);
  }
  encoder->setBytes(arguments_.data(), arguments_.size(), kArgumentBufferBindPoint);
  const auto& group = compute_pipeline_->thread_group_size();
  encoder->dispatchThreadgroups(MTL::Size::Make(arguments.x, arguments.y, arguments.z),
                                MTL::Size::Make(group[0], group[1], group[2]));
}

// ---- Copies ------------------------------------------------------------------------

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalRHIContext.cpp:565-581:
// buffer copies are blits on the context's blit encoder. Followed for every
// copy; a blit encoder stays open across consecutive copies.
void MetalContext::copy_buffer(Buffer& source, Buffer& destination, const BufferCopy& copy) {
  if (render_encoder_ != nullptr) {
    return;
  }
  blit_encoder()->copyFromBuffer(static_cast<MetalBuffer&>(source).buffer(), copy.source_offset,
                                 static_cast<MetalBuffer&>(destination).buffer(),
                                 copy.destination_offset, copy.size);
}

void MetalContext::copy_texture(Texture& source, Texture& destination,
                                const TextureCopyRegion& region) {
  if (render_encoder_ != nullptr) {
    return;
  }
  const TextureDescription& d = source.description();
  MTL::Size size = MTL::Size::Make(region.extent.width, region.extent.height, region.extent.depth);
  if (region.extent.width == 0U || region.extent.height == 0U) {
    const Extent2 mip = extent_at(d, region.source_mip);
    const std::uint32_t depth = d.dimension == TextureDimension::Texture3D
                                    ? std::max(d.extent.depth >> region.source_mip, 1U)
                                    : 1U;
    size = MTL::Size::Make(mip.width, mip.height, depth);
  }
  blit_encoder()->copyFromTexture(
      static_cast<MetalTexture&>(source).texture(), region.source_slice, region.source_mip,
      MTL::Origin::Make(region.source_origin.x, region.source_origin.y, region.source_origin.z),
      size, static_cast<MetalTexture&>(destination).texture(), region.destination_slice,
      region.destination_mip,
      MTL::Origin::Make(region.destination_origin.x, region.destination_origin.y,
                        region.destination_origin.z));
}

void MetalContext::copy_buffer_to_texture(Buffer& source, const BufferTextureRegion& region,
                                          Texture& destination) {
  if (render_encoder_ != nullptr) {
    return;
  }
  const TextureDescription& d = destination.description();
  const Extent2 mip = extent_at(d, region.mip);
  const std::uint32_t depth =
      d.dimension == TextureDimension::Texture3D ? std::max(d.extent.depth >> region.mip, 1U) : 1U;
  blit_encoder()->copyFromBuffer(static_cast<MetalBuffer&>(source).buffer(), region.buffer_offset,
                                 region.row_pitch, region.slice_pitch,
                                 MTL::Size::Make(mip.width, mip.height, depth),
                                 static_cast<MetalTexture&>(destination).texture(), region.slice,
                                 region.mip, MTL::Origin::Make(0, 0, 0));
}

// synchronization.hpp, Texture readback: the subresource lands at the
// staging layout's row pitch (staging_row_bytes rounded up to 256) and slice
// pitch. A depth or depth-stencil format reads back its depth plane alone,
// the bytes staging_row_bytes counts: a depth-only format is copied as is,
// a depth-stencil format with BlitOptionDepthFromDepthStencil.
void MetalContext::copy_texture_to_staging(Texture& source, std::uint8_t mip, std::uint16_t slice,
                                           StagingBuffer& destination,
                                           std::uint64_t destination_offset) {
  if (render_encoder_ != nullptr) {
    return;
  }
  const TextureDescription& d = source.description();
  const StagingLayout layout = staging_layout(d, mip);
  const FormatInfo info = format_info(d.format);
  const MTL::BlitOption option = info.is_depth && info.has_stencil
                                     ? MTL::BlitOptionDepthFromDepthStencil
                                     : MTL::BlitOptionNone;
  blit_encoder()->copyFromTexture(static_cast<MetalTexture&>(source).texture(), slice, mip,
                                  MTL::Origin::Make(0, 0, 0),
                                  MTL::Size::Make(layout.width, layout.height, layout.depth),
                                  static_cast<MetalStagingBuffer&>(destination).buffer(),
                                  destination_offset, layout.row_pitch, layout.slice_pitch, option);
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalUAV.cpp:713-738:
// RHICopyToStagingBuffer copies the source buffer into the staging buffer's
// shared shadow buffer with a blit. Followed.
void MetalContext::copy_buffer_to_staging(Buffer& source, StagingBuffer& destination,
                                          const BufferCopy& copy) {
  if (render_encoder_ != nullptr) {
    return;
  }
  blit_encoder()->copyFromBuffer(static_cast<MetalBuffer&>(source).buffer(), copy.source_offset,
                                 static_cast<MetalStagingBuffer&>(destination).buffer(),
                                 copy.destination_offset, copy.size);
}

// ---- Fences and queries ------------------------------------------------------------

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalUAV.cpp:803-831:
// RHIWriteGPUFence_TopOfPipe signals the fence's shared event at this point
// of the context's stream (MetalRHIContext.cpp:674). Followed: the event is
// signalled with the write's value on the frame's command buffer, after the
// open encoder ends (a command buffer encodes events between encoders).
void MetalContext::write_fence(GpuFence& fence) {
  if (render_encoder_ != nullptr) {
    return;
  }
  auto& metal_fence = static_cast<MetalGpuFence&>(fence);
  end_encoder();
  MTL::CommandBuffer* commands = command_buffer();
  commands->encodeSignalEvent(metal_fence.event(), metal_fence.begin_write());
  backend_->track_fence(metal_fence);
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalRHIRenderQuery.cpp:337-355
// and :373-377: an occlusion query sets the visibility result mode to
// Counting at its offset and back to Disabled at its end. Followed with the
// offset index * 8 in the pool's buffer, which the pass names.
void MetalContext::begin_query(QueryPool& pool, std::uint32_t index) {
  if (render_encoder_ == nullptr || pool.kind() != QueryKind::Occlusion) {
    return;
  }
  render_encoder_->setVisibilityResultMode(
      MTL::VisibilityResultModeCounting, static_cast<NS::UInteger>(index) * sizeof(std::uint64_t));
  // queries.hpp, Results: a query begun again is NotEnded until it is ended.
  static_cast<MetalQueryPool&>(pool).mark_ended(index, 0);
}

void MetalContext::end_query(QueryPool& pool, std::uint32_t index) {
  if (render_encoder_ == nullptr || pool.kind() != QueryKind::Occlusion) {
    return;
  }
  render_encoder_->setVisibilityResultMode(MTL::VisibilityResultModeDisabled, 0);
  static_cast<MetalQueryPool&>(pool).mark_ended(index, backend_->recording_frame());
}

// ---- Markers -------------------------------------------------------------------------

// command_list.hpp, Markers, under BW_RHI_DEBUG only. A marker begun while an
// encoder is open is a debug group on that encoder and closes with it; one
// begun between encoders is a debug group on the command buffer. A command
// buffer's group is popped with no encoder open: an open blit or compute
// encoder is ended first, and an open render pass defers the pop to the
// pass's end.
void MetalContext::begin_marker(std::string_view name) {
#if BW_RHI_DEBUG
  const AutoreleaseScope pool;
  NS::String* text = make_string(name);
  switch (encoder_kind_) {
  case EncoderKind::Render:
    render_encoder_->pushDebugGroup(text);
    markers_.push_back(Marker{MarkerWhere::Encoder, encoder_serial_});
    break;
  case EncoderKind::Blit:
    blit_encoder_->pushDebugGroup(text);
    markers_.push_back(Marker{MarkerWhere::Encoder, encoder_serial_});
    break;
  case EncoderKind::Compute:
    compute_encoder_->pushDebugGroup(text);
    markers_.push_back(Marker{MarkerWhere::Encoder, encoder_serial_});
    break;
  case EncoderKind::None:
    command_buffer()->pushDebugGroup(text);
    markers_.push_back(Marker{MarkerWhere::CommandBuffer, 0});
    break;
  }
#else
  static_cast<void>(name);
#endif
}

void MetalContext::end_marker() {
#if BW_RHI_DEBUG
  if (markers_.empty()) {
    return;
  }
  const Marker marker = markers_.back();
  markers_.pop_back();
  switch (marker.where) {
  case MarkerWhere::Closed:
    return;
  case MarkerWhere::Encoder:
    switch (encoder_kind_) {
    case EncoderKind::Render:
      render_encoder_->popDebugGroup();
      return;
    case EncoderKind::Blit:
      blit_encoder_->popDebugGroup();
      return;
    case EncoderKind::Compute:
      compute_encoder_->popDebugGroup();
      return;
    case EncoderKind::None:
      return;
    }
    return;
  case MarkerWhere::CommandBuffer:
    if (encoder_kind_ == EncoderKind::Render) {
      ++deferred_command_buffer_pops_;
      return;
    }
    end_encoder();
    command_buffer_->popDebugGroup();
    return;
  }
#endif
}

// ---- Frame hooks -----------------------------------------------------------------

MTL::CommandBuffer* MetalContext::finish_frame() {
  end_encoder();
  // Markers must balance within a list (command_list.hpp); any left open are
  // closed so the command buffer's groups balance before it is committed.
  while (!markers_.empty()) {
    if (markers_.back().where == MarkerWhere::CommandBuffer && command_buffer_ != nullptr) {
      command_buffer_->popDebugGroup();
    }
    markers_.pop_back();
  }
  MTL::CommandBuffer* commands = command_buffer_;
  command_buffer_ = nullptr;
  compute_pipeline_ = nullptr;
  index_buffer_ = nullptr;
  reset_bindings();
  return commands;
}

// command_list.hpp, Blocks: bindings do not survive end_frame. Unreal
// f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/Apple/MetalRHI/Private/MetalRHIContext.cpp:75-97:
// ResetContext resets the state cache and zeroes the static uniform buffer
// bindings (:93). Followed for the argument table: the draw block address,
// the static slot addresses and the root constants are zeroed, so the next
// frame's first draw cannot read a ring address of a frame the ring has
// rewound past. The three table addresses stay: they are the device's.
void MetalContext::reset_bindings() noexcept {
  const BindingStructure& binding = backend_->binding_structure();
  const std::uint32_t first = binding.draw_block().argument_offset;
  std::fill(arguments_.begin() + first, arguments_.end(), std::byte{0});
}

void MetalContext::reset_rings(std::uint64_t completed_frame) noexcept {
  for (Ring& ring : rings_) {
    if (ring.last_frame <= completed_frame) {
      ring.offset = 0;
    }
  }
}

} // namespace bw::rhi::metal
