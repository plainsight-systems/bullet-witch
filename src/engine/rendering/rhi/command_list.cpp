#include "bw/rendering/rhi/command_list.hpp"

#include "bw/rendering/rhi/backend.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>

namespace bw::rhi {

namespace {

// The stream's own encoding (command_list.hpp, Algorithm): a record is a
// Header, the command's payload struct padded to 8 bytes, then any trailing
// array padded to 8 bytes. Header::size is the whole record. Payloads are
// read back by copy, so no payload object is ever accessed in place; the
// trailing arrays are read in place as spans, which is why every record
// starts 8-aligned in storage whose base is aligned for any scalar.
enum class Command : std::uint8_t {
  BeginTransitions,
  EndTransitions,
  Transition,
  BeginRenderPass,
  EndRenderPass,
  SetGraphicsPipeline,
  SetComputePipeline,
  SetViewport,
  SetScissor,
  SetStencilReference,
  SetVertexBuffer,
  SetIndexBuffer,
  SetStaticBlock,
  SetDrawBlock,
  SetRootConstants,
  Draw,
  DrawIndexed,
  Dispatch,
  CopyBuffer,
  CopyTexture,
  CopyBufferToTexture,
  CopyTextureToStaging,
  CopyBufferToStaging,
  WriteFence,
  BeginQuery,
  EndQuery,
  BeginMarker,
  EndMarker,
};

struct Header {
  Command command{Command::EndRenderPass};
  std::uint32_t size{};
};

constexpr std::size_t kRecordAlignment = 8;
static_assert(sizeof(Header) == kRecordAlignment);
static_assert(alignof(Transition) <= kRecordAlignment);
static_assert(alignof(std::uint32_t) <= kRecordAlignment);
static_assert(__STDCPP_DEFAULT_NEW_ALIGNMENT__ >= kRecordAlignment);

[[nodiscard]] constexpr std::size_t padded(std::size_t size) noexcept {
  return (size + kRecordAlignment - 1U) & ~(kRecordAlignment - 1U);
}

struct NoPayload {};
struct TransitionsPayload {
  TransitionPipelines pipelines{};
  std::uint32_t count{};
};
struct RenderPassPayload {
  RenderPassDescription pass{};
  std::uint32_t name_length{};
};
template <class T> struct ObjectPayload {
  T* object{};
};
struct BufferBindingPayload {
  Buffer* buffer{};
  std::uint64_t offset{};
  std::uint32_t slot{};
};
struct IndexBufferPayload {
  Buffer* buffer{};
  std::uint64_t offset{};
  IndexFormat format{IndexFormat::Uint16};
};
struct DrawBlockPayload {
  shaders::LayoutHash layout{};
  std::uint64_t size{};
};
struct RootConstantsPayload {
  std::uint32_t first_word{};
  std::uint32_t count{};
};
struct CopyBufferPayload {
  Buffer* source{};
  Buffer* destination{};
  BufferCopy copy{};
};
struct CopyTexturePayload {
  Texture* source{};
  Texture* destination{};
  TextureCopyRegion region{};
};
struct CopyBufferToTexturePayload {
  Buffer* source{};
  Texture* destination{};
  BufferTextureRegion region{};
};
struct CopyTextureToStagingPayload {
  Texture* source{};
  StagingBuffer* destination{};
  std::uint64_t destination_offset{};
  std::uint16_t slice{};
  std::uint8_t mip{};
};
struct CopyBufferToStagingPayload {
  Buffer* source{};
  StagingBuffer* destination{};
  BufferCopy copy{};
};
struct QueryPayload {
  QueryPool* pool{};
  std::uint32_t index{};
};
struct MarkerPayload {
  std::uint32_t length{};
};

// Appends one record. Payload and trailing bytes are copied.
template <class Payload>
void append(std::vector<std::byte>& bytes, Command command, const Payload& payload,
            std::span<const std::byte> trailing = {}) {
  static_assert(std::is_trivially_copyable_v<Payload>);
  constexpr std::size_t payload_bytes = std::is_empty_v<Payload> ? 0U : padded(sizeof(Payload));
  const std::size_t record = sizeof(Header) + payload_bytes + padded(trailing.size());
  const std::size_t start = bytes.size();
  bytes.resize(start + record, std::byte{0});
  const Header header{command, static_cast<std::uint32_t>(record)};
  std::memcpy(bytes.data() + start, &header, sizeof(Header));
  if constexpr (!std::is_empty_v<Payload>) {
    std::memcpy(bytes.data() + start + sizeof(Header), &payload, sizeof(Payload));
  }
  if (!trailing.empty()) {
    std::memcpy(bytes.data() + start + sizeof(Header) + payload_bytes, trailing.data(),
                trailing.size());
  }
}

// One record being replayed.
class Record {
public:
  explicit Record(const std::byte* start) noexcept : start_(start) {
    std::memcpy(&header_, start, sizeof(Header));
  }
  [[nodiscard]] Command command() const noexcept { return header_.command; }
  [[nodiscard]] std::size_t size() const noexcept { return header_.size; }
  template <class Payload> [[nodiscard]] Payload payload() const noexcept {
    Payload value{};
    std::memcpy(&value, start_ + sizeof(Header), sizeof(Payload));
    return value;
  }
  // The trailing array after a payload of type Payload. The elements were
  // created in this storage by the memcpy in append (implicit-lifetime
  // types, [intro.object]/10), at an offset aligned for them.
  template <class Payload, class T>
  [[nodiscard]] std::span<const T> trailing(std::size_t count) const noexcept {
    if (count == 0U) {
      return {};
    }
    const std::byte* first = start_ + sizeof(Header) + padded(sizeof(Payload));
    return {std::launder(reinterpret_cast<const T*>(first)), count};
  }

private:
  const std::byte* start_;
  Header header_{};
};

[[nodiscard]] TransitionBatch transitions_of(const Record& record) noexcept {
  const auto payload = record.payload<TransitionsPayload>();
  return TransitionBatch{record.trailing<TransitionsPayload, Transition>(payload.count),
                         payload.pipelines};
}

} // namespace

CommandList::CommandList() = default;
CommandList::~CommandList() = default;

namespace {
void append_transitions(std::vector<std::byte>& bytes, Command command,
                        const TransitionBatch& batch) {
  const TransitionsPayload payload{batch.pipelines,
                                   static_cast<std::uint32_t>(batch.transitions.size())};
  append(bytes, command, payload, std::as_bytes(batch.transitions));
}
} // namespace

void CommandList::begin_transitions(const TransitionBatch& batch) {
  append_transitions(bytes_, Command::BeginTransitions, batch);
  ++command_count_;
}

void CommandList::end_transitions(const TransitionBatch& batch) {
  append_transitions(bytes_, Command::EndTransitions, batch);
  ++command_count_;
}

void CommandList::transition(const TransitionBatch& batch) {
  append_transitions(bytes_, Command::Transition, batch);
  ++command_count_;
}

// The list records without checks (command_list.hpp, Render passes);
// nesting and the pass description are checked by the validation layer when
// the list executes.
void CommandList::begin_render_pass(const RenderPassDescription& pass) {
  RenderPassPayload payload{pass, static_cast<std::uint32_t>(pass.name.size())};
  payload.pass.name = {};
  append(bytes_, Command::BeginRenderPass, payload, std::as_bytes(std::span{pass.name}));
  inside_pass_ = true;
  ++command_count_;
}

void CommandList::end_render_pass() {
  append(bytes_, Command::EndRenderPass, NoPayload{});
  inside_pass_ = false;
  ++command_count_;
}

void CommandList::set_graphics_pipeline(GraphicsPipeline& pipeline) {
  append(bytes_, Command::SetGraphicsPipeline, ObjectPayload<GraphicsPipeline>{&pipeline});
  ++command_count_;
}

void CommandList::set_compute_pipeline(ComputePipeline& pipeline) {
  append(bytes_, Command::SetComputePipeline, ObjectPayload<ComputePipeline>{&pipeline});
  ++command_count_;
}

void CommandList::set_viewport(const Viewport& viewport) {
  append(bytes_, Command::SetViewport, viewport);
  ++command_count_;
}

void CommandList::set_scissor(const ScissorRect& rect) {
  append(bytes_, Command::SetScissor, rect);
  ++command_count_;
}

void CommandList::set_stencil_reference(std::uint32_t reference) {
  append(bytes_, Command::SetStencilReference, reference);
  ++command_count_;
}

void CommandList::set_vertex_buffer(std::uint32_t slot, Buffer& buffer, std::uint64_t offset) {
  append(bytes_, Command::SetVertexBuffer, BufferBindingPayload{&buffer, offset, slot});
  ++command_count_;
}

void CommandList::set_index_buffer(Buffer& buffer, IndexFormat format, std::uint64_t offset) {
  append(bytes_, Command::SetIndexBuffer, IndexBufferPayload{&buffer, offset, format});
  ++command_count_;
}

void CommandList::set_static_block(std::uint32_t slot, Buffer& buffer, std::uint64_t offset) {
  append(bytes_, Command::SetStaticBlock, BufferBindingPayload{&buffer, offset, slot});
  ++command_count_;
}

void CommandList::set_draw_block(std::span<const std::byte> bytes, shaders::LayoutHash layout) {
  append(bytes_, Command::SetDrawBlock, DrawBlockPayload{layout, bytes.size()}, bytes);
  ++command_count_;
}

void CommandList::set_root_constants(std::uint32_t first_word,
                                     std::span<const std::uint32_t> words) {
  append(bytes_, Command::SetRootConstants,
         RootConstantsPayload{first_word, static_cast<std::uint32_t>(words.size())},
         std::as_bytes(words));
  ++command_count_;
}

void CommandList::draw(const DrawArguments& arguments) {
  append(bytes_, Command::Draw, arguments);
  ++command_count_;
}

void CommandList::draw_indexed(const DrawIndexedArguments& arguments) {
  append(bytes_, Command::DrawIndexed, arguments);
  ++command_count_;
}

void CommandList::dispatch(const DispatchArguments& arguments) {
  append(bytes_, Command::Dispatch, arguments);
  ++command_count_;
}

void CommandList::copy_buffer(Buffer& source, Buffer& destination, const BufferCopy& copy) {
  append(bytes_, Command::CopyBuffer, CopyBufferPayload{&source, &destination, copy});
  ++command_count_;
}

void CommandList::copy_texture(Texture& source, Texture& destination,
                               const TextureCopyRegion& region) {
  append(bytes_, Command::CopyTexture, CopyTexturePayload{&source, &destination, region});
  ++command_count_;
}

void CommandList::copy_buffer_to_texture(Buffer& source, const BufferTextureRegion& region,
                                         Texture& destination) {
  append(bytes_, Command::CopyBufferToTexture,
         CopyBufferToTexturePayload{&source, &destination, region});
  ++command_count_;
}

void CommandList::copy_texture_to_staging(Texture& source, std::uint8_t mip, std::uint16_t slice,
                                          StagingBuffer& destination,
                                          std::uint64_t destination_offset) {
  append(bytes_, Command::CopyTextureToStaging,
         CopyTextureToStagingPayload{&source, &destination, destination_offset, slice, mip});
  ++command_count_;
}

void CommandList::copy_buffer_to_staging(Buffer& source, StagingBuffer& destination,
                                         const BufferCopy& copy) {
  append(bytes_, Command::CopyBufferToStaging,
         CopyBufferToStagingPayload{&source, &destination, copy});
  ++command_count_;
}

void CommandList::write_fence(GpuFence& fence) {
  append(bytes_, Command::WriteFence, ObjectPayload<GpuFence>{&fence});
  ++command_count_;
}

void CommandList::begin_query(QueryPool& pool, std::uint32_t index) {
  append(bytes_, Command::BeginQuery, QueryPayload{&pool, index});
  ++command_count_;
}

void CommandList::end_query(QueryPool& pool, std::uint32_t index) {
  append(bytes_, Command::EndQuery, QueryPayload{&pool, index});
  ++command_count_;
}

#if BW_RHI_DEBUG
void CommandList::begin_marker(std::string_view name) {
  append(bytes_, Command::BeginMarker, MarkerPayload{static_cast<std::uint32_t>(name.size())},
         std::as_bytes(std::span{name}));
  ++marker_depth_;
  ++command_count_;
}

void CommandList::end_marker() {
  append(bytes_, Command::EndMarker, NoPayload{});
  if (marker_depth_ > 0U) {
    --marker_depth_;
  }
  ++command_count_;
}
#endif

// Walks the commands in recording order and runs each against the context.
// The list is left intact (a record is never destroyed by replay), and the
// walk stops at the first command that reports a failure. "Replayed at most
// once per recording" (command_list.hpp) is the caller's precondition
// (device.hpp, execute) and is not checked here.
Status<ExecuteError> CommandList::replay(Context& context) const {
  std::size_t offset = 0;
  while (offset < bytes_.size()) {
    const Record record(bytes_.data() + offset);
    offset += record.size();
    switch (record.command()) {
    case Command::BeginTransitions:
      context.begin_transitions(transitions_of(record));
      break;
    case Command::EndTransitions:
      context.end_transitions(transitions_of(record));
      break;
    case Command::Transition: {
      const TransitionBatch batch = transitions_of(record);
      context.begin_transitions(batch);
      context.end_transitions(batch);
      break;
    }
    case Command::BeginRenderPass: {
      const auto payload = record.payload<RenderPassPayload>();
      RenderPassDescription pass = payload.pass;
      const std::span<const char> name =
          record.trailing<RenderPassPayload, char>(payload.name_length);
      pass.name = std::string_view{name.data(), name.size()};
      context.begin_render_pass(pass);
      break;
    }
    case Command::EndRenderPass:
      context.end_render_pass();
      break;
    case Command::SetGraphicsPipeline:
      context.set_graphics_pipeline(*record.payload<ObjectPayload<GraphicsPipeline>>().object);
      break;
    case Command::SetComputePipeline:
      context.set_compute_pipeline(*record.payload<ObjectPayload<ComputePipeline>>().object);
      break;
    case Command::SetViewport:
      context.set_viewport(record.payload<Viewport>());
      break;
    case Command::SetScissor:
      context.set_scissor(record.payload<ScissorRect>());
      break;
    case Command::SetStencilReference:
      context.set_stencil_reference(record.payload<std::uint32_t>());
      break;
    case Command::SetVertexBuffer: {
      const auto payload = record.payload<BufferBindingPayload>();
      context.set_vertex_buffer(payload.slot, *payload.buffer, payload.offset);
      break;
    }
    case Command::SetIndexBuffer: {
      const auto payload = record.payload<IndexBufferPayload>();
      context.set_index_buffer(*payload.buffer, payload.format, payload.offset);
      break;
    }
    case Command::SetStaticBlock: {
      const auto payload = record.payload<BufferBindingPayload>();
      context.set_static_block(payload.slot, *payload.buffer, payload.offset);
      break;
    }
    case Command::SetDrawBlock: {
      const auto payload = record.payload<DrawBlockPayload>();
      const ExecuteError error = context.set_draw_block(
          record.trailing<DrawBlockPayload, std::byte>(static_cast<std::size_t>(payload.size)),
          payload.layout);
      if (error != ExecuteError::None) {
        return Status<ExecuteError>{error, "set_draw_block"};
      }
      break;
    }
    case Command::SetRootConstants: {
      const auto payload = record.payload<RootConstantsPayload>();
      context.set_root_constants(
          payload.first_word, record.trailing<RootConstantsPayload, std::uint32_t>(payload.count));
      break;
    }
    case Command::Draw:
      context.draw(record.payload<DrawArguments>());
      break;
    case Command::DrawIndexed:
      context.draw_indexed(record.payload<DrawIndexedArguments>());
      break;
    case Command::Dispatch:
      context.dispatch(record.payload<DispatchArguments>());
      break;
    case Command::CopyBuffer: {
      const auto payload = record.payload<CopyBufferPayload>();
      context.copy_buffer(*payload.source, *payload.destination, payload.copy);
      break;
    }
    case Command::CopyTexture: {
      const auto payload = record.payload<CopyTexturePayload>();
      context.copy_texture(*payload.source, *payload.destination, payload.region);
      break;
    }
    case Command::CopyBufferToTexture: {
      const auto payload = record.payload<CopyBufferToTexturePayload>();
      context.copy_buffer_to_texture(*payload.source, payload.region, *payload.destination);
      break;
    }
    case Command::CopyTextureToStaging: {
      const auto payload = record.payload<CopyTextureToStagingPayload>();
      context.copy_texture_to_staging(*payload.source, payload.mip, payload.slice,
                                      *payload.destination, payload.destination_offset);
      break;
    }
    case Command::CopyBufferToStaging: {
      const auto payload = record.payload<CopyBufferToStagingPayload>();
      context.copy_buffer_to_staging(*payload.source, *payload.destination, payload.copy);
      break;
    }
    case Command::WriteFence:
      context.write_fence(*record.payload<ObjectPayload<GpuFence>>().object);
      break;
    case Command::BeginQuery: {
      const auto payload = record.payload<QueryPayload>();
      context.begin_query(*payload.pool, payload.index);
      break;
    }
    case Command::EndQuery: {
      const auto payload = record.payload<QueryPayload>();
      context.end_query(*payload.pool, payload.index);
      break;
    }
    case Command::BeginMarker: {
      const auto payload = record.payload<MarkerPayload>();
      const std::span<const char> name = record.trailing<MarkerPayload, char>(payload.length);
      context.begin_marker(std::string_view{name.data(), name.size()});
      break;
    }
    case Command::EndMarker:
      context.end_marker();
      break;
    }
  }
  return Status<ExecuteError>{};
}

void CommandList::reset() noexcept {
  bytes_.clear();
  command_count_ = 0;
  marker_depth_ = 0;
  inside_pass_ = false;
}

} // namespace bw::rhi
