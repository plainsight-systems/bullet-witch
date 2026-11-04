#ifndef BW_RENDERING_RHI_BACKENDS_METAL_METAL_DEVICE_HPP
#define BW_RENDERING_RHI_BACKENDS_METAL_METAL_DEVICE_HPP
// The Metal backend (backend.hpp, Backend): the MTL::Device and its one
// command queue, the capability report, every creation entry point, the
// frame submission with a completion value per frame, and the waits.
//
// Contract (backend.hpp's, with what Metal adds).
//   Creation: MTL::CreateSystemDefaultDevice; NoAdapter when there is none;
//   RequiredCapabilityMissing naming TypedDescriptorArrays when argument
//   buffers are below tier 2 and ShaderFloor below the Apple7 family;
//   BackendInitializationFailed naming the step when the queue, an event,
//   the descriptor heaps or the rings cannot be created, when the
//   converter's bind points are not 0, 1, 2, or when the binding structure
//   is not the 160-byte layout the converter builds.
//   Frames: one MTL::CommandBuffer per frame (metal_context.hpp). submit_frame
//   presents every drawable the frame acquired on it (metal_viewport.hpp),
//   commits it, then a second command buffer that signals the frame's value
//   on the frame event; completed_frame is the event's value; the waits block
//   in the event and never spin. Fences written in the frame become
//   Submitted at its submit. take_gpu_fault reports, once, that either
//   command buffer of a frame completed with an error.
//   Viewports: the surface kind is MetalLayer; create_viewport configures
//   the layer and makes the back buffer (metal_viewport.hpp);
//   resize_viewport and set_vsync forward to the viewport.
//   Failure details: a failure's detail is a view into this backend's last
//   error text, valid until the next failing call of the backend; the device
//   serializes the creation calls that produce one.
//
// Core guidelines read: C.121 and C.122 (derives the abstract Backend), I.11
// (created into a unique_ptr), E.27 and E.4 (named failures, no half-made
// object), R.1 (every owned Metal object released in the destructor).
// Performance guidelines read: GPU.7 (one completion value per frame; waits
// name a frame), MEM.6 (budgets from options).
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/format.hpp"

#include "metal_api.hpp"
#include "metal_context.hpp"
#include "metal_formats.hpp"
#include "metal_memory.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace bw::rhi::metal {

class MetalGpuFence;
class MetalViewport;

class MetalBackend final : public Backend {
public:
  [[nodiscard]] static BackendResult create(const BackendOptions& options);
  MetalBackend(const MetalBackend&) = delete;
  MetalBackend& operator=(const MetalBackend&) = delete;
  ~MetalBackend() override;

  [[nodiscard]] const Capabilities& capabilities() const noexcept override { return capabilities_; }
  [[nodiscard]] FormatCapability format_capabilities(Format format) const noexcept override;

  [[nodiscard]] BufferResult create_buffer(const BufferDescription& description,
                                           std::span<const std::byte> initial_data) override;
  [[nodiscard]] TextureResult
  create_texture(const TextureDescription& description,
                 std::span<const SubresourceData> initial_data) override;
  [[nodiscard]] ShaderResourceViewResult
  create_shader_resource_view(Ref<Buffer> buffer, const BufferViewDescription& view,
                              DescriptorSlot slot) override;
  [[nodiscard]] ShaderResourceViewResult
  create_shader_resource_view(Ref<Texture> texture, const TextureViewDescription& view,
                              DescriptorSlot slot) override;
  [[nodiscard]] UnorderedAccessViewResult
  create_unordered_access_view(Ref<Buffer> buffer, const BufferViewDescription& view,
                               DescriptorSlot slot) override;
  [[nodiscard]] UnorderedAccessViewResult
  create_unordered_access_view(Ref<Texture> texture, const TextureViewDescription& view,
                               DescriptorSlot slot) override;
  [[nodiscard]] SamplerResult create_sampler(const SamplerDescription& description,
                                             DescriptorSlot slot) override;
  [[nodiscard]] RasterizerStateResult
  create_rasterizer_state(const RasterizerDescription& description) override;
  [[nodiscard]] DepthStencilStateResult
  create_depth_stencil_state(const DepthStencilDescription& description) override;
  [[nodiscard]] BlendStateResult create_blend_state(const BlendDescription& description) override;
  [[nodiscard]] GraphicsPipelineResult
  create_graphics_pipeline(const GraphicsPipelineDescription& description,
                           const PipelineBinding& binding) override;
  [[nodiscard]] ComputePipelineResult
  create_compute_pipeline(const ComputePipelineDescription& description,
                          const PipelineBinding& binding) override;
  [[nodiscard]] GpuFenceResult create_fence(std::string_view debug_name) override;
  [[nodiscard]] StagingBufferResult create_staging_buffer(std::uint64_t size,
                                                          std::string_view debug_name) override;
  [[nodiscard]] QueryPoolResult create_query_pool(const QueryPoolDescription& description) override;
  [[nodiscard]] ViewportResult create_viewport(const ViewportDescription& description) override;
  [[nodiscard]] Status<ViewportError> resize_viewport(Viewport& viewport,
                                                      Extent2 pixel_size) override;
  void set_vsync(Viewport& viewport, bool vsync) override;

  [[nodiscard]] LockResult lock_buffer(Buffer& buffer, BufferRange range) override;
  [[nodiscard]] LockError unlock_buffer(Buffer& buffer) override;
  [[nodiscard]] StagingLockResult lock_staging(StagingBuffer& staging, GpuFence& fence,
                                               WaitPolicy policy) override;
  [[nodiscard]] StagingError unlock_staging(StagingBuffer& staging) override;
  [[nodiscard]] QueryResult query_result(QueryPool& pool, std::uint32_t index,
                                         WaitPolicy policy) override;

  [[nodiscard]] Context& context() noexcept override { return *context_; }
  void submit_frame(std::uint64_t frame) override;
  [[nodiscard]] std::uint64_t completed_frame() const noexcept override;
  void wait_for_frame(std::uint64_t frame) override;
  void wait_idle() override;
  [[nodiscard]] bool take_gpu_fault() noexcept override;
  void reset_frame_resources(std::uint64_t completed_frame) override;

  // For the context.
  [[nodiscard]] MTL::CommandQueue& queue() const noexcept { return *queue_; }
  [[nodiscard]] ResourceMemory& memory() noexcept { return *memory_; }
  [[nodiscard]] const DescriptorHeaps& descriptor_heaps() const noexcept { return *descriptors_; }
  [[nodiscard]] const WritableResources& writable_resources() const noexcept { return *writable_; }
  [[nodiscard]] const BindingStructure& binding_structure() const noexcept {
    return *options_.binding_structure;
  }
  // The frame whose commands are being recorded: the last submitted plus 1.
  [[nodiscard]] std::uint64_t recording_frame() const noexcept {
    return recording_frame_.load(std::memory_order_acquire);
  }
  // A fence written in the frame being recorded; it becomes Submitted at
  // that frame's submit_frame.
  void track_fence(MetalGpuFence& fence);
  // A viewport whose back buffer the frame being recorded acquired; its
  // drawable is presented at that frame's submit_frame. Once per frame per
  // viewport, however many passes bind it.
  void track_viewport(MetalViewport& viewport);

private:
  MetalBackend(const BackendOptions& options, MTL::Device* device, MTL::CommandQueue* queue,
               MTL::SharedEvent* frame_event, const Capabilities& capabilities,
               FormatSupport format_support);
  // Fills the backend's remaining objects; returns the failing step's name or
  // an empty view.
  [[nodiscard]] std::string_view initialize(MTL::Buffer* resource_heap, MTL::Buffer* sampler_heap);
  // Records an error text and returns a view of it (the contract above).
  [[nodiscard]] std::string_view fail_with(std::string text);
  // Copies bytes into a private buffer or texture through a blit and waits
  // for it; returns an empty view on success, else the API's error text.
  [[nodiscard]] std::string_view upload_buffer(MTL::Buffer& destination,
                                               std::span<const std::byte> bytes);
  [[nodiscard]] std::string_view upload_texture(MTL::Texture& destination,
                                                const TextureDescription& description,
                                                std::span<const SubresourceData> data);
  [[nodiscard]] std::string_view finish_upload(MTL::CommandBuffer& commands);
  // The library and function of an artifact (both owned by the caller), or
  // nulls with the error and its detail set.
  struct LoadedFunction {
    MTL::Library* library{};
    MTL::Function* function{};
  };
  [[nodiscard]] LoadedFunction load_function(const shaders::Artifact& artifact,
                                             PipelineError& error, std::string_view& detail);

  BackendOptions options_;
  MTL::Device* device_;
  MTL::CommandQueue* queue_;
  MTL::SharedEvent* frame_event_;
  Capabilities capabilities_;
  FormatSupport format_support_;
  std::atomic<std::uint64_t> recording_frame_{1};
  std::atomic<std::uint64_t> submitted_frame_{0};
  std::unique_ptr<ResourceMemory> memory_{};
  std::unique_ptr<DescriptorHeaps> descriptors_{};
  std::unique_ptr<WritableResources> writable_{};
  std::unique_ptr<MetalContext> context_{};
  std::vector<MetalGpuFence*> fences_written_{};
  // Observers: a viewport is destroyed by the delete queue only after the
  // frame that acquired its back buffer completed, which is after that
  // frame's submit_frame emptied this list.
  std::vector<MetalViewport*> viewports_acquired_{};
  // Set by a completed handler when submitted work ended in error
  // (take_gpu_fault); shared with the handlers, which may outlive the backend.
  std::shared_ptr<std::atomic<bool>> gpu_fault_{std::make_shared<std::atomic<bool>>(false)};
  std::string error_text_{};
};

} // namespace bw::rhi::metal
#endif
