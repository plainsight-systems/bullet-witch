#ifndef BW_RENDERING_RHI_BACKENDS_METAL_METAL_CONTEXT_HPP
#define BW_RENDERING_RHI_BACKENDS_METAL_METAL_CONTEXT_HPP
// The Metal backend's one execution context (backend.hpp, Context): it turns
// replayed commands into the frame's MTL::CommandBuffer and its encoders.
//
// Contract.
//   Driven by the one submission thread (backend.hpp). The frame's command
//   buffer is created at the first command of the frame that needs one and
//   handed to the backend by finish_frame, which ends any open encoder
//   first. One encoder is open at a time: a render encoder from
//   begin_render_pass to end_render_pass, else a blit encoder for copies or
//   a compute encoder for dispatches, ended when another kind is needed.
//   Binding (binding_structure.hpp): the context keeps the 160-byte top
//   level argument buffer of the engine root layout: the three table
//   addresses (constant for the device), the draw block's address, the
//   static slots' addresses and the root constants. A draw or dispatch sets
//   it with set*Bytes at the converter's argument buffer bind point, binds
//   the two descriptor heaps at their bind points, declares the resource
//   heaps resident (useHeaps) and the transient ring and the descriptor heaps
//   resident (useResources, read) once per encoder, and draws through the
//   converter runtime.
//   Transient ring (invariant 7): one shared buffer of
//   transient_bytes_per_frame per frame in flight. set_draw_block copies the
//   bytes to the frame's ring at a 256-byte aligned offset; the ring never
//   grows; a frame's ring is rewound by reset_rings once that frame has
//   completed. finish_frame zeroes the draw block, static slot and root
//   constant entries of the argument table (command_list.hpp, Blocks:
//   bindings do not survive end_frame).
//
// Core guidelines read: CP.1 (one thread drives the context; the state here
// is unsynchronized by contract), I.1 (every budget comes from options),
// E.27 (set_draw_block returns its named error). Performance guidelines
// read: MEM.1 and MEM.4 (a per-frame ring rewound at one point), GPU.7 (the
// ring of a frame in flight is never written), GPU.9 (no API allocation per
// draw: the argument buffer goes through set*Bytes, blocks through the ring).
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/binding_structure.hpp"
#include "bw/rendering/rhi/build.hpp"

#include "metal_api.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace bw::rhi::metal {

class MetalBackend;
class MetalGraphicsPipeline;
class MetalComputePipeline;
class MetalQueryPool;

// The alignment of every bound buffer offset: draw block offsets in the ring,
// and reported as Capabilities::uniform_buffer_alignment, the alignment a
// static block's offset must have.
inline constexpr std::uint32_t kBufferOffsetAlignment = 256;
// The placement alignment of a buffer a shader may read or write
// (metal_device.cpp, create_buffer).
inline constexpr std::uint32_t kBufferBackedTextureOffsetAlignment = 1024;

class MetalContext final : public Context {
public:
  // rings: one shared, untracked buffer per frame in flight, owned.
  MetalContext(MetalBackend& backend, const std::vector<MTL::Buffer*>& rings);
  MetalContext(const MetalContext&) = delete;
  MetalContext& operator=(const MetalContext&) = delete;
  ~MetalContext() override;

  void begin_transitions(const TransitionBatch& batch) override;
  void end_transitions(const TransitionBatch& batch) override;
  void begin_render_pass(const RenderPassDescription& pass) override;
  void end_render_pass() override;
  void set_graphics_pipeline(GraphicsPipeline& pipeline) override;
  void set_compute_pipeline(ComputePipeline& pipeline) override;
  void set_viewport(const Viewport& viewport) override;
  void set_scissor(const ScissorRect& rect) override;
  void set_stencil_reference(std::uint32_t reference) override;
  void set_vertex_buffer(std::uint32_t slot, Buffer& buffer, std::uint64_t offset) override;
  void set_index_buffer(Buffer& buffer, IndexFormat format, std::uint64_t offset) override;
  void set_static_block(std::uint32_t slot, Buffer& buffer, std::uint64_t offset) override;
  [[nodiscard]] ExecuteError set_draw_block(std::span<const std::byte> bytes,
                                            shaders::LayoutHash layout) override;
  void set_root_constants(std::uint32_t first_word, std::span<const std::uint32_t> words) override;
  void draw(const DrawArguments& arguments) override;
  void draw_indexed(const DrawIndexedArguments& arguments) override;
  void dispatch(const DispatchArguments& arguments) override;
  void copy_buffer(Buffer& source, Buffer& destination, const BufferCopy& copy) override;
  void copy_texture(Texture& source, Texture& destination,
                    const TextureCopyRegion& region) override;
  void copy_buffer_to_texture(Buffer& source, const BufferTextureRegion& region,
                              Texture& destination) override;
  void copy_texture_to_staging(Texture& source, std::uint8_t mip, std::uint16_t slice,
                               StagingBuffer& destination,
                               std::uint64_t destination_offset) override;
  void copy_buffer_to_staging(Buffer& source, StagingBuffer& destination,
                              const BufferCopy& copy) override;
  void write_fence(GpuFence& fence) override;
  void begin_query(QueryPool& pool, std::uint32_t index) override;
  void end_query(QueryPool& pool, std::uint32_t index) override;
  void begin_marker(std::string_view name) override;
  void end_marker() override;

  // The backend's frame hooks. finish_frame ends any open encoder and
  // returns the frame's command buffer (owned by the caller from here, +1),
  // or null when the frame recorded no work. reset_rings rewinds every ring
  // whose last frame is at or below completed_frame.
  [[nodiscard]] MTL::CommandBuffer* finish_frame();
  void reset_rings(std::uint64_t completed_frame) noexcept;

private:
  enum class EncoderKind : std::uint8_t { None, Render, Blit, Compute };
  struct Ring {
    MTL::Buffer* buffer{};
    std::uint64_t offset{};
    std::uint64_t last_frame{};
  };
  // Where a marker's debug group was pushed: on an encoder (identified by
  // its serial), on the command buffer, or on an encoder that has since
  // ended and closed it.
  enum class MarkerWhere : std::uint8_t { Encoder, CommandBuffer, Closed };
  struct Marker {
    MarkerWhere where{MarkerWhere::Closed};
    std::uint64_t encoder_serial{};
  };

  MTL::CommandBuffer* command_buffer();
  void end_encoder();
  MTL::BlitCommandEncoder* blit_encoder();
  MTL::ComputeCommandEncoder* compute_encoder();
  void declare_residency(MTL::RenderCommandEncoder& encoder);
  void declare_residency(MTL::ComputeCommandEncoder& encoder);
  void declare_written(MTL::RenderCommandEncoder& encoder);
  void declare_written(MTL::ComputeCommandEncoder& encoder);
  void reset_bindings() noexcept;
  void bind_render_arguments();
  [[nodiscard]] Ring& frame_ring() noexcept;

  MetalBackend* backend_;
  std::vector<Ring> rings_;
  MTL::CommandBuffer* command_buffer_{};
  MTL::RenderCommandEncoder* render_encoder_{};
  MTL::BlitCommandEncoder* blit_encoder_{};
  MTL::ComputeCommandEncoder* compute_encoder_{};
  EncoderKind encoder_kind_{EncoderKind::None};
  std::uint64_t encoder_serial_{};
  // Residency already declared on the open encoder.
  std::uint64_t resident_heap_generation_{};
  bool heaps_resident_{};
  bool ring_resident_{};
  std::vector<MTL::Heap*> heap_scratch_{};
  bool written_resident_{};
  std::uint64_t written_generation_{};
  std::vector<const MTL::Resource*> written_scratch_{};
  // Render pass state.
  std::uint32_t pass_width_{};
  std::uint32_t pass_height_{};
  MetalGraphicsPipeline* graphics_pipeline_{};
  MetalComputePipeline* compute_pipeline_{};
  MTL::PrimitiveType primitive_{MTL::PrimitiveTypeTriangle};
  MTL::Buffer* index_buffer_{};
  std::uint64_t index_offset_{};
  IndexFormat index_format_{IndexFormat::Uint16};
  // The top-level argument buffer (binding_structure.hpp).
  std::array<std::byte, kEngineArgumentBufferBytes> arguments_{};
  std::vector<Marker> markers_{};
  std::uint32_t deferred_command_buffer_pops_{};
};

} // namespace bw::rhi::metal
#endif
