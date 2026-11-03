#ifndef BW_RENDERING_RHI_BACKENDS_METAL_METAL_OBJECTS_HPP
#define BW_RENDERING_RHI_BACKENDS_METAL_METAL_OBJECTS_HPP
// The Metal objects behind the RHI's resource classes (resources.hpp,
// pipeline_states.hpp, synchronization.hpp, queries.hpp). Each class derives
// from the header's class, owns its Metal objects (+1) and releases them in
// its destructor, which only the deferred delete queue runs (lifetime.hpp),
// after the GPU has completed every frame that named the object.
//
// Core guidelines read: C.35 (destructors run through the base, from the
// queue), C.67 (copy is suppressed by Resource), R.1 (one owner releases).
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/queries.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"

#include "metal_api.hpp"
#include "metal_memory.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace bw::rhi::metal {

class MetalBuffer final : public Buffer {
public:
  // buffer: owned. memory: where placement was taken; outlives this object.
  MetalBuffer(DeferredDeleteQueue& queue, const BufferDescription& description, MTL::Buffer* buffer,
              HeapPlacement placement, ResourceMemory& memory) noexcept
      : Buffer(queue, description), buffer_(buffer), placement_(placement), memory_(&memory) {}
  [[nodiscard]] MTL::Buffer* buffer() const noexcept { return buffer_; }
  // The persistent host mapping's lock state (resources.hpp, Locks); the
  // device mutex serializes lock_buffer and unlock_buffer.
  [[nodiscard]] bool locked() const noexcept { return locked_; }
  void set_locked(bool locked) noexcept { locked_ = locked; }

private:
  ~MetalBuffer() override;
  MTL::Buffer* buffer_;
  HeapPlacement placement_;
  ResourceMemory* memory_;
  bool locked_{};
};

class MetalTexture final : public Texture {
public:
  MetalTexture(DeferredDeleteQueue& queue, const TextureDescription& description,
               MTL::Texture* texture, HeapPlacement placement, ResourceMemory& memory) noexcept
      : Texture(queue, description), texture_(texture), placement_(placement), memory_(&memory) {}
  [[nodiscard]] MTL::Texture* texture() const noexcept { return texture_; }

private:
  ~MetalTexture() override;
  MTL::Texture* texture_;
  HeapPlacement placement_;
  ResourceMemory* memory_;
};

// A view keeps its resource alive through View's Ref; texture_view is the
// Metal texture view it created, owned, or null for a buffer view or a view
// of the whole texture in its own format (the entry names the texture then).
class MetalShaderResourceView final : public ShaderResourceView {
public:
  MetalShaderResourceView(DeferredDeleteQueue& queue, Ref<Viewable> resource, DescriptorSlot slot,
                          DescriptorAllocators& pools, MTL::Texture* texture_view) noexcept
      : ShaderResourceView(queue, std::move(resource), slot, pools), texture_view_(texture_view) {}

private:
  ~MetalShaderResourceView() override;
  MTL::Texture* texture_view_;
};

// written: the Metal resource the view's descriptor names, already added to
// `writable` by the creator; the destructor removes it.
class MetalUnorderedAccessView final : public UnorderedAccessView {
public:
  MetalUnorderedAccessView(DeferredDeleteQueue& queue, Ref<Viewable> resource, DescriptorSlot slot,
                           DescriptorAllocators& pools, MTL::Texture* texture_view,
                           MTL::Resource& written, WritableResources& writable) noexcept
      : UnorderedAccessView(queue, std::move(resource), slot, pools), texture_view_(texture_view),
        written_(&written), writable_(&writable) {}

private:
  ~MetalUnorderedAccessView() override;
  MTL::Texture* texture_view_;
  MTL::Resource* written_;
  WritableResources* writable_;
};

class MetalSampler final : public Sampler {
public:
  MetalSampler(DeferredDeleteQueue& queue, const SamplerDescription& description,
               DescriptorSlot slot, MTL::SamplerState* state) noexcept
      : Sampler(queue, description, slot), state_(state) {}

private:
  ~MetalSampler() override;
  MTL::SamplerState* state_;
};

// Rasterizer and blend state are encoder and pipeline state in Metal; the
// objects carry their descriptions only (pipeline_states.hpp) and are read
// at set_graphics_pipeline and at pipeline creation.
class MetalRasterizerState final : public RasterizerState {
public:
  MetalRasterizerState(DeferredDeleteQueue& queue,
                       const RasterizerDescription& description) noexcept
      : RasterizerState(queue, description) {}
};
class MetalBlendState final : public BlendState {
public:
  MetalBlendState(DeferredDeleteQueue& queue, const BlendDescription& description) noexcept
      : BlendState(queue, description) {}
};

class MetalDepthStencilState final : public DepthStencilState {
public:
  MetalDepthStencilState(DeferredDeleteQueue& queue, const DepthStencilDescription& description,
                         MTL::DepthStencilState* state) noexcept
      : DepthStencilState(queue, description), state_(state) {}
  [[nodiscard]] MTL::DepthStencilState* state() const noexcept { return state_; }

private:
  ~MetalDepthStencilState() override;
  MTL::DepthStencilState* state_;
};

class MetalGraphicsPipeline final : public GraphicsPipeline {
public:
  MetalGraphicsPipeline(DeferredDeleteQueue& queue, const GraphicsPipelineDescription& description,
                        const PipelineBinding& binding, MTL::RenderPipelineState* state) noexcept
      : GraphicsPipeline(queue, description, binding), state_(state) {}
  [[nodiscard]] MTL::RenderPipelineState* state() const noexcept { return state_; }

private:
  ~MetalGraphicsPipeline() override;
  MTL::RenderPipelineState* state_;
};

class MetalComputePipeline final : public ComputePipeline {
public:
  MetalComputePipeline(DeferredDeleteQueue& queue, const ComputePipelineDescription& description,
                       const PipelineBinding& binding, MTL::ComputePipelineState* state) noexcept
      : ComputePipeline(queue, description, binding), state_(state) {}
  [[nodiscard]] MTL::ComputePipelineState* state() const noexcept { return state_; }

private:
  ~MetalComputePipeline() override;
  MTL::ComputePipelineState* state_;
};

// synchronization.hpp, GpuFence: an MTL::SharedEvent of its own and the
// value of the last write. The phase is written by the submission thread
// (write_fence, submit_frame, reset) and read by any thread (state, wait).
class MetalGpuFence final : public GpuFence {
public:
  MetalGpuFence(DeferredDeleteQueue& queue, std::string_view name, MTL::SharedEvent* event) noexcept
      : GpuFence(queue, name), event_(event) {}
  [[nodiscard]] FenceState state() const noexcept override;
  void wait() noexcept override;
  void reset() noexcept override;
  [[nodiscard]] MTL::SharedEvent* event() const noexcept { return event_; }
  // The value the next write signals; moves the fence to Pending.
  [[nodiscard]] std::uint64_t begin_write() noexcept;
  // The frame holding the write was committed: Pending becomes Submitted.
  void mark_submitted() noexcept;

private:
  ~MetalGpuFence() override;
  MTL::SharedEvent* event_;
  std::atomic<std::uint64_t> value_{0};
  std::atomic<FenceState> phase_{FenceState::Unwritten};
};

class MetalStagingBuffer final : public StagingBuffer {
public:
  MetalStagingBuffer(DeferredDeleteQueue& queue, std::uint64_t size, std::string_view name,
                     MTL::Buffer* buffer, HeapPlacement placement, ResourceMemory& memory) noexcept
      : StagingBuffer(queue, size, name), buffer_(buffer), placement_(placement), memory_(&memory) {
  }
  [[nodiscard]] MTL::Buffer* buffer() const noexcept { return buffer_; }
  [[nodiscard]] bool locked() const noexcept { return locked_; }
  void set_locked(bool locked) noexcept { locked_ = locked; }

private:
  ~MetalStagingBuffer() override;
  MTL::Buffer* buffer_;
  HeapPlacement placement_;
  ResourceMemory* memory_;
  bool locked_{};
};

// queries.hpp, Occlusion: one 8-byte visibility result per index in a shared
// buffer the render pass names; ended_frame[index] is the frame whose replay
// ended the query, 0 when never ended.
class MetalQueryPool final : public QueryPool {
public:
  MetalQueryPool(DeferredDeleteQueue& queue, const QueryPoolDescription& description,
                 MTL::Buffer* results, HeapPlacement placement, ResourceMemory& memory)
      : QueryPool(queue, description), results_(results), placement_(placement), memory_(&memory),
        ended_frame_(std::make_unique<std::atomic<std::uint64_t>[]>(description.count)) {}
  [[nodiscard]] MTL::Buffer* results() const noexcept { return results_; }
  void mark_ended(std::uint32_t index, std::uint64_t frame) noexcept;
  [[nodiscard]] std::uint64_t ended_frame(std::uint32_t index) const noexcept;
  [[nodiscard]] std::uint64_t result(std::uint32_t index) const noexcept;

private:
  ~MetalQueryPool() override;
  MTL::Buffer* results_;
  HeapPlacement placement_;
  ResourceMemory* memory_;
  std::unique_ptr<std::atomic<std::uint64_t>[]> ended_frame_;
};

} // namespace bw::rhi::metal
#endif
