#ifndef BW_TESTS_UNIT_RENDERING_RHI_VALIDATION_OBJECTS_HPP
#define BW_TESTS_UNIT_RENDERING_RHI_VALIDATION_OBJECTS_HPP
// Test-local boundary for the validation layer's unit test: a recording
// Backend and Context derived from the public pure-virtual interfaces
// (backend.hpp), the objects the commands name, and a capturing reporter.
// The layer is reached only through wrap_with_validation; the recording
// backend is what the layer wraps, so what it records is what the layer
// forwarded. The recording context executes nothing and writes no tracked
// access; its creation entries return BackendRejected and the scenarios
// never create through it.
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/binding_structure.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/descriptor_allocator.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/queries.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/transitions.hpp"
#include "bw/rendering/rhi/viewport.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace bw_rhi_validation_test {

// The arguments of one call as text, so the test compares what the inner
// received with what the caller passed field by field.
class Args {
public:
  Args& operator<<(std::uint64_t value) {
    text_ += std::to_string(value);
    text_ += ' ';
    return *this;
  }
  Args& operator<<(std::int64_t value) {
    text_ += std::to_string(value);
    text_ += ' ';
    return *this;
  }
  Args& operator<<(float value) {
    text_ += std::to_string(value);
    text_ += ' ';
    return *this;
  }
  Args& operator<<(const void* pointer) {
    text_ += std::to_string(reinterpret_cast<std::uintptr_t>(pointer));
    text_ += ' ';
    return *this;
  }
  Args& operator<<(std::string_view text) {
    text_ += '"';
    text_ += text;
    text_ += "\" ";
    return *this;
  }
  template <class E>
    requires std::is_enum_v<E>
  Args& operator<<(E value) {
    return *this << static_cast<std::uint64_t>(static_cast<std::underlying_type_t<E>>(value));
  }
  [[nodiscard]] std::string str() const { return text_; }

private:
  std::string text_{};
};

inline std::string describe(const bw::rhi::TransitionBatch& batch) {
  Args args;
  for (const bw::rhi::Transition& transition : batch.transitions) {
    args << static_cast<const void*>(transition.resource) << transition.before << transition.after;
  }
  args << batch.pipelines.source << batch.pipelines.destination;
  return args.str();
}

inline std::string describe(const bw::rhi::RenderPassDescription& pass) {
  Args args;
  args << static_cast<std::uint64_t>(pass.color_count);
  for (const bw::rhi::ColorTarget& color : pass.colors) {
    args << static_cast<const void*>(color.texture) << static_cast<std::uint64_t>(color.mip)
         << static_cast<std::uint64_t>(color.slice) << color.load << color.store;
  }
  const bw::rhi::DepthStencilTarget& depth = pass.depth_stencil;
  args << static_cast<const void*>(depth.texture) << depth.depth_load << depth.depth_store
       << depth.stencil_load << depth.stencil_store
       << static_cast<std::uint64_t>(depth.depth_read_only)
       << static_cast<std::uint64_t>(depth.stencil_read_only)
       << static_cast<const void*>(pass.occlusion_queries) << pass.name;
  return args.str();
}

inline std::string describe(const bw::rhi::ViewportRect& viewport) {
  return (Args{} << viewport.x << viewport.y << viewport.width << viewport.height
                 << viewport.min_depth << viewport.max_depth)
      .str();
}

inline std::string describe(const bw::rhi::ScissorRect& rect) {
  return (Args{} << static_cast<std::int64_t>(rect.x) << static_cast<std::int64_t>(rect.y)
                 << static_cast<std::uint64_t>(rect.width)
                 << static_cast<std::uint64_t>(rect.height))
      .str();
}

inline std::string describe(const bw::rhi::DrawArguments& draw) {
  return (Args{} << static_cast<std::uint64_t>(draw.vertex_count)
                 << static_cast<std::uint64_t>(draw.instance_count)
                 << static_cast<std::uint64_t>(draw.first_vertex)
                 << static_cast<std::uint64_t>(draw.first_instance))
      .str();
}

inline std::string describe(const bw::rhi::DrawIndexedArguments& draw) {
  return (Args{} << static_cast<std::uint64_t>(draw.index_count)
                 << static_cast<std::uint64_t>(draw.instance_count)
                 << static_cast<std::uint64_t>(draw.first_index)
                 << static_cast<std::int64_t>(draw.base_vertex)
                 << static_cast<std::uint64_t>(draw.first_instance))
      .str();
}

inline std::string describe(const bw::rhi::DispatchArguments& groups) {
  return (Args{} << static_cast<std::uint64_t>(groups.x) << static_cast<std::uint64_t>(groups.y)
                 << static_cast<std::uint64_t>(groups.z))
      .str();
}

inline std::string describe(const bw::rhi::BufferCopy& copy) {
  return (Args{} << copy.source_offset << copy.destination_offset << copy.size).str();
}

inline std::string describe(const bw::rhi::TextureCopyRegion& region) {
  return (Args{} << static_cast<std::uint64_t>(region.source_mip)
                 << static_cast<std::uint64_t>(region.source_slice)
                 << static_cast<std::uint64_t>(region.source_origin.x)
                 << static_cast<std::uint64_t>(region.source_origin.y)
                 << static_cast<std::uint64_t>(region.source_origin.z)
                 << static_cast<std::uint64_t>(region.destination_mip)
                 << static_cast<std::uint64_t>(region.destination_slice)
                 << static_cast<std::uint64_t>(region.destination_origin.x)
                 << static_cast<std::uint64_t>(region.destination_origin.y)
                 << static_cast<std::uint64_t>(region.destination_origin.z)
                 << static_cast<std::uint64_t>(region.extent.width)
                 << static_cast<std::uint64_t>(region.extent.height)
                 << static_cast<std::uint64_t>(region.extent.depth))
      .str();
}

inline std::string describe(const bw::rhi::BufferTextureRegion& region) {
  return (Args{} << region.buffer_offset << static_cast<std::uint64_t>(region.row_pitch)
                 << static_cast<std::uint64_t>(region.slice_pitch)
                 << static_cast<std::uint64_t>(region.mip)
                 << static_cast<std::uint64_t>(region.slice))
      .str();
}

inline std::string describe(std::span<const std::byte> bytes) {
  return (Args{} << static_cast<const void*>(bytes.data())
                 << static_cast<std::uint64_t>(bytes.size()))
      .str();
}

inline std::string describe(std::span<const std::uint32_t> words) {
  Args args;
  for (const std::uint32_t word : words) {
    args << static_cast<std::uint64_t>(word);
  }
  return args.str();
}

inline std::string describe(const bw::rhi::BufferDescription& d) {
  return (Args{} << d.size << static_cast<std::uint64_t>(d.stride) << d.usage << d.initial_access
                 << d.debug_name)
      .str();
}

inline std::string describe(const bw::rhi::TextureDescription& d) {
  return (Args{} << static_cast<std::uint64_t>(d.extent.width)
                 << static_cast<std::uint64_t>(d.extent.height)
                 << static_cast<std::uint64_t>(d.extent.depth)
                 << static_cast<std::uint64_t>(d.array_size)
                 << static_cast<std::uint64_t>(d.mip_levels)
                 << static_cast<std::uint64_t>(d.sample_count) << d.dimension << d.format << d.usage
                 << d.initial_access << d.debug_name)
      .str();
}

inline std::string describe(std::span<const bw::rhi::SubresourceData> data) {
  return (Args{} << static_cast<const void*>(data.data())
                 << static_cast<std::uint64_t>(data.size()))
      .str();
}

inline std::string describe(const bw::rhi::BufferViewDescription& view) {
  return (Args{} << view.offset << view.size).str();
}

inline std::string describe(const bw::rhi::TextureViewDescription& view) {
  return (Args{} << view.format << view.dimension << view.plane
                 << static_cast<std::uint64_t>(view.first_mip)
                 << static_cast<std::uint64_t>(view.mip_count)
                 << static_cast<std::uint64_t>(view.first_slice)
                 << static_cast<std::uint64_t>(view.slice_count))
      .str();
}

inline std::string describe(bw::rhi::DescriptorSlot slot) {
  return (Args{} << static_cast<std::uint64_t>(slot.index) << slot.array).str();
}

inline std::string describe(const bw::rhi::SamplerDescription& d) {
  return (Args{} << d.filter << d.address_u << d.mip_bias << d.max_mip
                 << static_cast<std::uint64_t>(d.max_anisotropy) << d.border_color << d.compare)
      .str();
}

inline std::string describe(const bw::rhi::RasterizerDescription& d) {
  return (Args{} << d.fill << d.cull << d.depth_bias << d.slope_scale_depth_bias << d.depth_clip)
      .str();
}

inline std::string describe(const bw::rhi::DepthStencilDescription& d) {
  return (Args{} << static_cast<std::uint64_t>(d.depth_write) << d.depth_test
                 << static_cast<std::uint64_t>(d.stencil_read_mask)
                 << static_cast<std::uint64_t>(d.stencil_write_mask))
      .str();
}

inline std::string describe(const bw::rhi::BlendDescription& d) {
  return (Args{} << d.targets[0].color_source << d.targets[0].color_destination
                 << d.targets[0].write_mask << static_cast<std::uint64_t>(d.independent)
                 << static_cast<std::uint64_t>(d.alpha_to_coverage))
      .str();
}

inline std::string describe(const bw::rhi::PipelineBinding& b) {
  return (Args{} << b.draw_block_hash.value << static_cast<std::uint64_t>(b.root_constant_words)
                 << static_cast<std::uint64_t>(b.descriptor_arrays)
                 << static_cast<std::uint64_t>(b.static_slot_mask)
                 << static_cast<std::uint64_t>(b.uses_draw_block))
      .str();
}

inline std::string describe(const bw::rhi::GraphicsPipelineDescription& d) {
  return (Args{} << static_cast<const void*>(d.vertex_shader)
                 << static_cast<const void*>(d.pixel_shader)
                 << static_cast<const void*>(d.rasterizer)
                 << static_cast<const void*>(d.depth_stencil) << static_cast<const void*>(d.blend)
                 << static_cast<std::uint64_t>(d.targets.color_count) << d.targets.color_formats[0]
                 << d.primitive << d.debug_name)
      .str();
}

inline std::string describe(const bw::rhi::ComputePipelineDescription& d) {
  return (Args{} << static_cast<const void*>(d.compute_shader) << d.debug_name).str();
}

inline std::string describe(const bw::rhi::QueryPoolDescription& d) {
  return (Args{} << d.kind << static_cast<std::uint64_t>(d.count) << d.debug_name).str();
}

inline std::string describe(const bw::rhi::ViewportDescription& d) {
  return (Args{} << d.window.kind << static_cast<const void*>(d.window.surface)
                 << static_cast<std::uint64_t>(d.pixel_size.width)
                 << static_cast<std::uint64_t>(d.pixel_size.height) << d.format
                 << static_cast<std::uint64_t>(d.vsync) << d.debug_name)
      .str();
}

struct Call {
  std::string name;
  std::string args;
};

class RecordingContext final : public bw::rhi::Context {
public:
  std::vector<Call> calls;
  // What set_draw_block and begin_render_pass return.
  bw::rhi::ExecuteError draw_block_result{bw::rhi::ExecuteError::None};
  bw::rhi::ExecuteError render_pass_result{bw::rhi::ExecuteError::None};

  void begin_transitions(const bw::rhi::TransitionBatch& batch) override {
    record("begin_transitions", describe(batch));
  }
  void end_transitions(const bw::rhi::TransitionBatch& batch) override {
    record("end_transitions", describe(batch));
  }
  bw::rhi::ExecuteError begin_render_pass(const bw::rhi::RenderPassDescription& pass) override {
    record("begin_render_pass", describe(pass));
    return render_pass_result;
  }
  void end_render_pass() override { record("end_render_pass", {}); }
  void set_graphics_pipeline(bw::rhi::GraphicsPipeline& pipeline) override {
    record("set_graphics_pipeline", (Args{} << static_cast<const void*>(&pipeline)).str());
  }
  void set_compute_pipeline(bw::rhi::ComputePipeline& pipeline) override {
    record("set_compute_pipeline", (Args{} << static_cast<const void*>(&pipeline)).str());
  }
  void set_viewport(const bw::rhi::ViewportRect& viewport) override {
    record("set_viewport", describe(viewport));
  }
  void set_scissor(const bw::rhi::ScissorRect& rect) override {
    record("set_scissor", describe(rect));
  }
  void set_stencil_reference(std::uint32_t reference) override {
    record("set_stencil_reference", (Args{} << static_cast<std::uint64_t>(reference)).str());
  }
  void set_vertex_buffer(std::uint32_t slot, bw::rhi::Buffer& buffer,
                         std::uint64_t offset) override {
    record("set_vertex_buffer", (Args{} << static_cast<std::uint64_t>(slot)
                                        << static_cast<const void*>(&buffer) << offset)
                                    .str());
  }
  void set_index_buffer(bw::rhi::Buffer& buffer, bw::rhi::IndexFormat format,
                        std::uint64_t offset) override {
    record("set_index_buffer",
           (Args{} << static_cast<const void*>(&buffer) << format << offset).str());
  }
  void set_static_block(std::uint32_t slot, bw::rhi::Buffer& buffer,
                        std::uint64_t offset) override {
    record("set_static_block", (Args{} << static_cast<std::uint64_t>(slot)
                                       << static_cast<const void*>(&buffer) << offset)
                                   .str());
  }
  bw::rhi::ExecuteError set_draw_block(std::span<const std::byte> bytes,
                                       bw::shaders::LayoutHash layout) override {
    record("set_draw_block", describe(bytes) + (Args{} << layout.value).str());
    return draw_block_result;
  }
  void set_root_constants(std::uint32_t first_word, std::span<const std::uint32_t> words) override {
    record("set_root_constants",
           (Args{} << static_cast<std::uint64_t>(first_word)).str() + describe(words));
  }
  void draw(const bw::rhi::DrawArguments& arguments) override {
    record("draw", describe(arguments));
  }
  void draw_indexed(const bw::rhi::DrawIndexedArguments& arguments) override {
    record("draw_indexed", describe(arguments));
  }
  void dispatch(const bw::rhi::DispatchArguments& arguments) override {
    record("dispatch", describe(arguments));
  }
  // The signature is Context's (backend.hpp).
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void copy_buffer(bw::rhi::Buffer& source, bw::rhi::Buffer& destination,
                   const bw::rhi::BufferCopy& copy) override {
    record("copy_buffer",
           (Args{} << static_cast<const void*>(&source) << static_cast<const void*>(&destination))
                   .str() +
               describe(copy));
  }
  // The signature is Context's (backend.hpp).
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void copy_texture(bw::rhi::Texture& source, bw::rhi::Texture& destination,
                    const bw::rhi::TextureCopyRegion& region) override {
    record("copy_texture",
           (Args{} << static_cast<const void*>(&source) << static_cast<const void*>(&destination))
                   .str() +
               describe(region));
  }
  void copy_buffer_to_texture(bw::rhi::Buffer& source, const bw::rhi::BufferTextureRegion& region,
                              bw::rhi::Texture& destination) override {
    record("copy_buffer_to_texture",
           (Args{} << static_cast<const void*>(&source) << static_cast<const void*>(&destination))
                   .str() +
               describe(region));
  }
  void copy_texture_to_staging(bw::rhi::Texture& source, std::uint8_t mip, std::uint16_t slice,
                               bw::rhi::StagingBuffer& destination,
                               std::uint64_t destination_offset) override {
    record("copy_texture_to_staging",
           (Args{} << static_cast<const void*>(&source) << static_cast<std::uint64_t>(mip)
                   << static_cast<std::uint64_t>(slice) << static_cast<const void*>(&destination)
                   << destination_offset)
               .str());
  }
  void copy_buffer_to_staging(bw::rhi::Buffer& source, bw::rhi::StagingBuffer& destination,
                              const bw::rhi::BufferCopy& copy) override {
    record("copy_buffer_to_staging",
           (Args{} << static_cast<const void*>(&source) << static_cast<const void*>(&destination))
                   .str() +
               describe(copy));
  }
  void write_fence(bw::rhi::GpuFence& fence) override {
    record("write_fence", (Args{} << static_cast<const void*>(&fence)).str());
  }
  void begin_query(bw::rhi::QueryPool& pool, std::uint32_t index) override {
    record("begin_query",
           (Args{} << static_cast<const void*>(&pool) << static_cast<std::uint64_t>(index)).str());
  }
  void end_query(bw::rhi::QueryPool& pool, std::uint32_t index) override {
    record("end_query",
           (Args{} << static_cast<const void*>(&pool) << static_cast<std::uint64_t>(index)).str());
  }
  void begin_marker(std::string_view name) override {
    record("begin_marker", (Args{} << name).str());
  }
  void end_marker() override { record("end_marker", {}); }

private:
  void record(std::string_view name, std::string args) {
    calls.push_back(Call{std::string(name), std::move(args)});
  }
};

inline constexpr std::string_view kRejected = "recording backend";

class RecordingBackend final : public bw::rhi::Backend {
public:
  explicit RecordingBackend(const bw::rhi::Capabilities& capabilities) noexcept
      : capabilities_(capabilities) {}

  // Calls on this backend, in order; the context's are in context_.calls.
  // The noexcept const entries count instead of recording, so they never
  // allocate.
  std::vector<Call> calls;
  mutable std::uint32_t format_capabilities_calls{};
  mutable bw::rhi::Format last_format{bw::rhi::Format::Unknown};
  mutable std::uint32_t completed_frame_calls{};
  std::uint32_t take_gpu_fault_calls{};
  // What completed_frame and format_capabilities return.
  std::uint64_t completed{41};
  // What the next take_gpu_fault returns; cleared by it (backend.hpp).
  bool gpu_fault{};
  bw::rhi::FormatCapability format_capability{bw::rhi::FormatCapability::Blendable};

  [[nodiscard]] RecordingContext& recording_context() noexcept { return context_; }

  [[nodiscard]] const bw::rhi::Capabilities& capabilities() const noexcept override {
    return capabilities_;
  }
  [[nodiscard]] bw::rhi::FormatCapability
  format_capabilities(bw::rhi::Format format) const noexcept override {
    ++format_capabilities_calls;
    last_format = format;
    return format_capability;
  }
  bw::rhi::BufferResult create_buffer(const bw::rhi::BufferDescription& description,
                                      std::span<const std::byte> initial_data) override {
    record("create_buffer", describe(description) + describe(initial_data));
    return bw::rhi::BufferResult::failure(bw::rhi::ResourceError::BackendRejected, kRejected);
  }
  bw::rhi::TextureResult
  create_texture(const bw::rhi::TextureDescription& description,
                 std::span<const bw::rhi::SubresourceData> initial_data) override {
    record("create_texture", describe(description) + describe(initial_data));
    return bw::rhi::TextureResult::failure(bw::rhi::ResourceError::BackendRejected, kRejected);
  }
  bw::rhi::ShaderResourceViewResult
  create_shader_resource_view(bw::rhi::Ref<bw::rhi::Buffer> buffer,
                              const bw::rhi::BufferViewDescription& view,
                              bw::rhi::DescriptorSlot slot) override {
    record("create_shader_resource_view(buffer)",
           (Args{} << static_cast<const void*>(buffer.get())).str() + describe(view) +
               describe(slot));
    return bw::rhi::ShaderResourceViewResult::failure(bw::rhi::ResourceError::BackendRejected,
                                                      kRejected);
  }
  bw::rhi::ShaderResourceViewResult
  create_shader_resource_view(bw::rhi::Ref<bw::rhi::Texture> texture,
                              const bw::rhi::TextureViewDescription& view,
                              bw::rhi::DescriptorSlot slot) override {
    record("create_shader_resource_view(texture)",
           (Args{} << static_cast<const void*>(texture.get())).str() + describe(view) +
               describe(slot));
    return bw::rhi::ShaderResourceViewResult::failure(bw::rhi::ResourceError::BackendRejected,
                                                      kRejected);
  }
  bw::rhi::UnorderedAccessViewResult
  create_unordered_access_view(bw::rhi::Ref<bw::rhi::Buffer> buffer,
                               const bw::rhi::BufferViewDescription& view,
                               bw::rhi::DescriptorSlot slot) override {
    record("create_unordered_access_view(buffer)",
           (Args{} << static_cast<const void*>(buffer.get())).str() + describe(view) +
               describe(slot));
    return bw::rhi::UnorderedAccessViewResult::failure(bw::rhi::ResourceError::BackendRejected,
                                                       kRejected);
  }
  bw::rhi::UnorderedAccessViewResult
  create_unordered_access_view(bw::rhi::Ref<bw::rhi::Texture> texture,
                               const bw::rhi::TextureViewDescription& view,
                               bw::rhi::DescriptorSlot slot) override {
    record("create_unordered_access_view(texture)",
           (Args{} << static_cast<const void*>(texture.get())).str() + describe(view) +
               describe(slot));
    return bw::rhi::UnorderedAccessViewResult::failure(bw::rhi::ResourceError::BackendRejected,
                                                       kRejected);
  }
  bw::rhi::SamplerResult create_sampler(const bw::rhi::SamplerDescription& description,
                                        bw::rhi::DescriptorSlot slot) override {
    record("create_sampler", describe(description) + describe(slot));
    return bw::rhi::SamplerResult::failure(bw::rhi::ResourceError::BackendRejected, kRejected);
  }
  bw::rhi::RasterizerStateResult
  create_rasterizer_state(const bw::rhi::RasterizerDescription& description) override {
    record("create_rasterizer_state", describe(description));
    return bw::rhi::RasterizerStateResult::failure(bw::rhi::ResourceError::BackendRejected,
                                                   kRejected);
  }
  bw::rhi::DepthStencilStateResult
  create_depth_stencil_state(const bw::rhi::DepthStencilDescription& description) override {
    record("create_depth_stencil_state", describe(description));
    return bw::rhi::DepthStencilStateResult::failure(bw::rhi::ResourceError::BackendRejected,
                                                     kRejected);
  }
  bw::rhi::BlendStateResult
  create_blend_state(const bw::rhi::BlendDescription& description) override {
    record("create_blend_state", describe(description));
    return bw::rhi::BlendStateResult::failure(bw::rhi::ResourceError::BackendRejected, kRejected);
  }
  bw::rhi::GraphicsPipelineResult
  create_graphics_pipeline(const bw::rhi::GraphicsPipelineDescription& description,
                           const bw::rhi::PipelineBinding& binding) override {
    record("create_graphics_pipeline", describe(description) + describe(binding));
    return bw::rhi::GraphicsPipelineResult::failure(bw::rhi::PipelineError::BackendRejected,
                                                    kRejected);
  }
  bw::rhi::ComputePipelineResult
  create_compute_pipeline(const bw::rhi::ComputePipelineDescription& description,
                          const bw::rhi::PipelineBinding& binding) override {
    record("create_compute_pipeline", describe(description) + describe(binding));
    return bw::rhi::ComputePipelineResult::failure(bw::rhi::PipelineError::BackendRejected,
                                                   kRejected);
  }
  bw::rhi::GpuFenceResult create_fence(std::string_view debug_name) override {
    record("create_fence", (Args{} << debug_name).str());
    return bw::rhi::GpuFenceResult::failure(bw::rhi::ResourceError::BackendRejected, kRejected);
  }
  bw::rhi::StagingBufferResult create_staging_buffer(std::uint64_t size,
                                                     std::string_view debug_name) override {
    record("create_staging_buffer", (Args{} << size << debug_name).str());
    return bw::rhi::StagingBufferResult::failure(bw::rhi::ResourceError::BackendRejected,
                                                 kRejected);
  }
  bw::rhi::QueryPoolResult
  create_query_pool(const bw::rhi::QueryPoolDescription& description) override {
    record("create_query_pool", describe(description));
    return bw::rhi::QueryPoolResult::failure(bw::rhi::QueryError::BackendRejected, kRejected);
  }
  bw::rhi::ViewportResult
  create_viewport(const bw::rhi::ViewportDescription& description) override {
    record("create_viewport", describe(description));
    return bw::rhi::ViewportResult::failure(bw::rhi::ViewportError::BackendRejected, kRejected);
  }
  bw::rhi::Status<bw::rhi::ViewportError> resize_viewport(bw::rhi::Viewport& viewport,
                                                          bw::rhi::Extent2 pixel_size) override {
    record("resize_viewport", (Args{} << static_cast<const void*>(&viewport)
                                      << static_cast<std::uint64_t>(pixel_size.width)
                                      << static_cast<std::uint64_t>(pixel_size.height))
                                  .str());
    return bw::rhi::Status<bw::rhi::ViewportError>{bw::rhi::ViewportError::BackendRejected,
                                                   kRejected};
  }
  void set_vsync(bw::rhi::Viewport& viewport, bool vsync) override {
    record(
        "set_vsync",
        (Args{} << static_cast<const void*>(&viewport) << static_cast<std::uint64_t>(vsync)).str());
  }
  bw::rhi::LockResult lock_buffer(bw::rhi::Buffer& buffer, bw::rhi::BufferRange range) override {
    record("lock_buffer",
           (Args{} << static_cast<const void*>(&buffer) << range.offset << range.size).str());
    return bw::rhi::LockResult::failure(bw::rhi::LockError::AlreadyLocked, kRejected);
  }
  bw::rhi::LockError unlock_buffer(bw::rhi::Buffer& buffer) override {
    record("unlock_buffer", (Args{} << static_cast<const void*>(&buffer)).str());
    return bw::rhi::LockError::NotLocked;
  }
  bw::rhi::StagingLockResult lock_staging(bw::rhi::StagingBuffer& staging, bw::rhi::GpuFence& fence,
                                          bw::rhi::WaitPolicy policy) override {
    record("lock_staging", (Args{} << static_cast<const void*>(&staging)
                                   << static_cast<const void*>(&fence) << policy)
                               .str());
    return bw::rhi::StagingLockResult::failure(bw::rhi::StagingError::NotReady, kRejected);
  }
  bw::rhi::StagingError unlock_staging(bw::rhi::StagingBuffer& staging) override {
    record("unlock_staging", (Args{} << static_cast<const void*>(&staging)).str());
    return bw::rhi::StagingError::NotLocked;
  }
  bw::rhi::QueryResult query_result(bw::rhi::QueryPool& pool, std::uint32_t index,
                                    bw::rhi::WaitPolicy policy) override {
    record("query_result", (Args{} << static_cast<const void*>(&pool)
                                   << static_cast<std::uint64_t>(index) << policy)
                               .str());
    return bw::rhi::QueryResult::failure(bw::rhi::QueryError::NotReady, kRejected);
  }
  [[nodiscard]] bw::rhi::Context& context() noexcept override { return context_; }
  void submit_frame(std::uint64_t frame) override {
    record("submit_frame", (Args{} << frame).str());
  }
  [[nodiscard]] std::uint64_t completed_frame() const noexcept override {
    ++completed_frame_calls;
    return completed;
  }
  void wait_for_frame(std::uint64_t frame) override {
    record("wait_for_frame", (Args{} << frame).str());
  }
  void wait_idle() override { record("wait_idle", {}); }
  // Counts instead of recording, as the other noexcept entries do.
  [[nodiscard]] bool take_gpu_fault() noexcept override {
    ++take_gpu_fault_calls;
    const bool fault = gpu_fault;
    gpu_fault = false;
    return fault;
  }
  void reset_frame_resources(std::uint64_t completed_frame) override {
    record("reset_frame_resources", (Args{} << completed_frame).str());
  }

private:
  void record(std::string_view name, std::string args) {
    calls.push_back(Call{std::string(name), std::move(args)});
  }

  bw::rhi::Capabilities capabilities_;
  RecordingContext context_{};
};

// Pipelines with a chosen binding (pipeline_objects.hpp's take none).
class BoundGraphicsPipeline final : public bw::rhi::GraphicsPipeline {
public:
  BoundGraphicsPipeline(bw::rhi::DeferredDeleteQueue& queue,
                        const bw::rhi::GraphicsPipelineDescription& description,
                        const bw::rhi::PipelineBinding& binding) noexcept
      : GraphicsPipeline(queue, description, binding) {}

private:
  ~BoundGraphicsPipeline() override = default;
};

class BoundComputePipeline final : public bw::rhi::ComputePipeline {
public:
  BoundComputePipeline(bw::rhi::DeferredDeleteQueue& queue, const bw::rhi::PipelineBinding& binding,
                       std::string_view name) noexcept
      : ComputePipeline(queue, bw::rhi::ComputePipelineDescription{nullptr, name}, binding) {}

private:
  ~BoundComputePipeline() override = default;
};

class TestFence final : public bw::rhi::GpuFence {
public:
  explicit TestFence(bw::rhi::DeferredDeleteQueue& queue) noexcept : GpuFence(queue, "Fence") {}
  [[nodiscard]] bw::rhi::FenceState state() const noexcept override {
    return bw::rhi::FenceState::Unwritten;
  }
  void wait() noexcept override {}
  void reset() noexcept override {}

private:
  ~TestFence() override = default;
};

class TestStagingBuffer final : public bw::rhi::StagingBuffer {
public:
  explicit TestStagingBuffer(bw::rhi::DeferredDeleteQueue& queue) noexcept
      : StagingBuffer(queue, 4096, "Staging") {}

private:
  ~TestStagingBuffer() override = default;
};

class TestQueryPool final : public bw::rhi::QueryPool {
public:
  TestQueryPool(bw::rhi::DeferredDeleteQueue& queue,
                const bw::rhi::QueryPoolDescription& description) noexcept
      : QueryPool(queue, description) {}

private:
  ~TestQueryPool() override = default;
};

// A viewport the backend entries are called with; it holds no back buffer
// and is never presented (the recording backend executes nothing).
class TestViewport final : public bw::rhi::Viewport {
public:
  TestViewport(bw::rhi::DeferredDeleteQueue& queue,
               const bw::rhi::ViewportDescription& description) noexcept
      : Viewport(queue, description) {}

private:
  ~TestViewport() override = default;
};

// What the capturing reporter received: fixed capacity, so the noexcept
// reporter never allocates; count keeps counting past the capacity so an
// overflow is visible.
struct Captured {
  std::array<bw::rhi::ValidationFailure, 64> failures{};
  std::size_t count{};
};

// The capturing reporter: `user` is a Captured.
inline void capture(const bw::rhi::ValidationFailure& failure, void* user) noexcept {
  auto* captured = static_cast<Captured*>(user);
  if (captured->count < captured->failures.size()) {
    captured->failures[captured->count] = failure;
  }
  ++captured->count;
}

} // namespace bw_rhi_validation_test

#endif
