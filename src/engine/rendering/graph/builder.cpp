#include "bw/rendering/graph/builder.hpp"

#include "graph_state.hpp"

#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/compiled_frame.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Responsibility: the declaration side of bw/rendering/graph/builder.hpp:
// recording a frame's resources, passes and marks, checking each
// declaration against the rules of declaration.hpp as it is made and
// keeping the first rule broken for compile (contract 1), and the
// execution side the executor and RecordContext call (contract 4).
//
// A broken rule is recorded and reported by compile rather than raised at the
// call (builder.hpp, contract 1).
namespace bw::graph {

namespace {

[[nodiscard]] bool is_depth_access(Access access) noexcept {
  return access == Access::DepthTest || access == Access::DepthWrite;
}

[[nodiscard]] bool states_contents(Access access) noexcept {
  return access == Access::Attachment || access == Access::DepthWrite;
}

} // namespace

// ---- RecordContext --------------------------------------------------------------

rhi::Texture* RecordContext::texture(TextureHandle handle) const noexcept {
  for (const TextureUse& use : graph_->texture_uses(pass_)) {
    if (use.texture == handle) {
      return graph_->texture(handle);
    }
  }
  return nullptr;
}

rhi::Buffer* RecordContext::buffer(BufferHandle handle) const noexcept {
  for (const BufferUse& use : graph_->buffer_uses(pass_)) {
    if (use.buffer == handle) {
      return graph_->buffer(handle);
    }
  }
  return nullptr;
}

// ---- FrameGraph: lifetime of the object -------------------------------------------

FrameGraph::FrameGraph() : state_(std::make_unique<State>()) {}

FrameGraph::~FrameGraph() = default;

void FrameGraph::reset() noexcept {
  State& s = *state_;
  s.textures.clear();
  s.buffers.clear();
  s.passes.clear();
  s.ranges.clear();
  s.texture_uses.clear();
  s.buffer_uses.clear();
  s.texture_readbacks.clear();
  s.buffer_readbacks.clear();
  s.first_error = CompileResult{};
  // A 16-bit count that wraps; a handle of the frame before no longer matches.
  s.frame = static_cast<std::uint16_t>(s.frame + 1U);
}

// ---- Declaration ------------------------------------------------------------------

namespace {

// Keeps the first rule broken in declaration order (builder.hpp, contract 1).
void note(CompileResult& first, GraphError error, std::string_view pass,
          std::string_view resource) noexcept {
  if (first.ok()) {
    first = CompileResult{error, pass, resource};
  }
}

[[nodiscard]] std::string_view name_of(const TextureRecord& record) noexcept {
  if (!record.imported) {
    return record.description.debug_name;
  }
  return record.import.texture != nullptr ? record.import.texture->debug_name()
                                          : std::string_view{};
}

[[nodiscard]] std::string_view name_of(const BufferRecord& record) noexcept {
  if (!record.imported) {
    return record.description.debug_name;
  }
  return record.import.buffer != nullptr ? record.import.buffer->debug_name() : std::string_view{};
}

// The extent and format of a texture as a pass binds it; nothing for an
// imported texture whose pointer is null (already ImportedStateInvalid).
struct Shape {
  bool known{};
  rhi::Extent2 extent{};
  rhi::Format format{rhi::Format::Unknown};
};

[[nodiscard]] Shape shape_of(const TextureRecord& record) noexcept {
  if (!record.imported) {
    return Shape{true, record.description.extent, record.description.format};
  }
  if (record.import.texture == nullptr) {
    return Shape{};
  }
  const rhi::TextureDescription& d = record.import.texture->description();
  return Shape{true, rhi::Extent2{d.extent.width, d.extent.height}, d.format};
}

// A handle names a resource of this frame: made since the last reset (its
// frame is the builder's) and indexing one of the frame's resources
// (declaration.hpp, contract 1).
template <class Handle, class Records>
[[nodiscard]] bool names_resource(Handle handle, std::uint16_t frame,
                                  const Records& records) noexcept {
  return handle.valid() && handle.frame == frame && handle.index < records.size();
}

[[nodiscard]] bool valid_import(rhi::Access entry, rhi::Access exit) noexcept {
  return rhi::is_valid_access(entry) && rhi::is_valid_access(exit);
}

} // namespace

TextureHandle FrameGraph::create_texture(const FrameTextureDescription& description) {
  State& s = *state_;
  if (s.textures.size() >= kInvalidHandle) {
    note(s.first_error, GraphError::HandleInvalid, {}, description.debug_name);
    return TextureHandle{};
  }
  TextureRecord record{};
  record.description = description;
  s.textures.push_back(std::move(record));
  s.texture_readbacks.emplace_back();
  return TextureHandle{static_cast<std::uint16_t>(s.textures.size() - 1U), s.frame};
}

BufferHandle FrameGraph::create_buffer(const FrameBufferDescription& description) {
  State& s = *state_;
  if (s.buffers.size() >= kInvalidHandle) {
    note(s.first_error, GraphError::HandleInvalid, {}, description.debug_name);
    return BufferHandle{};
  }
  BufferRecord record{};
  record.description = description;
  s.buffers.push_back(std::move(record));
  s.buffer_readbacks.emplace_back();
  return BufferHandle{static_cast<std::uint16_t>(s.buffers.size() - 1U), s.frame};
}

TextureHandle FrameGraph::import_texture(const ImportedTexture& imported) {
  State& s = *state_;
  TextureRecord record{};
  record.imported = true;
  record.import = imported;
  if (s.textures.size() >= kInvalidHandle) {
    note(s.first_error, GraphError::HandleInvalid, {}, name_of(record));
    return TextureHandle{};
  }
  if (imported.texture == nullptr || !valid_import(imported.entry, imported.exit)) {
    note(s.first_error, GraphError::ImportedStateInvalid, {}, name_of(record));
  }
  s.textures.push_back(std::move(record));
  s.texture_readbacks.emplace_back();
  return TextureHandle{static_cast<std::uint16_t>(s.textures.size() - 1U), s.frame};
}

BufferHandle FrameGraph::import_buffer(const ImportedBuffer& imported) {
  State& s = *state_;
  BufferRecord record{};
  record.imported = true;
  record.import = imported;
  if (s.buffers.size() >= kInvalidHandle) {
    note(s.first_error, GraphError::HandleInvalid, {}, name_of(record));
    return BufferHandle{};
  }
  if (imported.buffer == nullptr || !valid_import(imported.entry, imported.exit)) {
    note(s.first_error, GraphError::ImportedStateInvalid, {}, name_of(record));
  }
  s.buffers.push_back(std::move(record));
  s.buffer_readbacks.emplace_back();
  return BufferHandle{static_cast<std::uint16_t>(s.buffers.size() - 1U), s.frame};
}

namespace {

// The rules of declaration.hpp contract 4 for one pass, each use in order
// and then the pass as a whole; the first broken is returned with the
// resource it concerns.
struct PassCheck {
  GraphError error{GraphError::None};
  std::string_view resource{};
};

PassCheck check_pass(const std::vector<TextureRecord>& texture_records,
                     const std::vector<BufferRecord>& buffer_records, std::uint16_t frame,
                     const PassDeclaration& pass) noexcept;

} // namespace

void FrameGraph::add_pass(const PassDeclaration& pass) {
  State& s = *state_;
  const TextureUse* textures_before = s.texture_uses.data();
  const BufferUse* buffers_before = s.buffer_uses.data();
  PassRange range{};
  range.first_texture = static_cast<std::uint32_t>(s.texture_uses.size());
  range.texture_count = static_cast<std::uint32_t>(pass.textures.size());
  range.first_buffer = static_cast<std::uint32_t>(s.buffer_uses.size());
  range.buffer_count = static_cast<std::uint32_t>(pass.buffers.size());
  s.texture_uses.insert(s.texture_uses.end(), pass.textures.begin(), pass.textures.end());
  s.buffer_uses.insert(s.buffer_uses.end(), pass.buffers.begin(), pass.buffers.end());
  s.ranges.push_back(range);
  s.passes.push_back(pass);
  // The copies may have moved the use arrays; every pass views them again.
  const bool moved =
      s.texture_uses.data() != textures_before || s.buffer_uses.data() != buffers_before;
  const std::size_t first = moved ? 0U : s.passes.size() - 1U;
  for (std::size_t i = first; i < s.passes.size(); ++i) {
    const PassRange& r = s.ranges[i];
    s.passes[i].textures =
        std::span<const TextureUse>(s.texture_uses).subspan(r.first_texture, r.texture_count);
    s.passes[i].buffers =
        std::span<const BufferUse>(s.buffer_uses).subspan(r.first_buffer, r.buffer_count);
  }
  const PassCheck check = check_pass(s.textures, s.buffers, s.frame, s.passes.back());
  if (check.error != GraphError::None) {
    note(s.first_error, check.error, pass.name, check.resource);
  }
}

namespace {

PassCheck check_pass(const std::vector<TextureRecord>& texture_records,
                     const std::vector<BufferRecord>& buffer_records, std::uint16_t frame,
                     const PassDeclaration& pass) noexcept {
  const std::span<const TextureUse> textures = pass.textures;
  for (std::size_t i = 0; i < textures.size(); ++i) {
    const TextureUse& use = textures[i];
    if (!names_resource(use.texture, frame, texture_records)) {
      return PassCheck{GraphError::HandleInvalid, {}};
    }
    const TextureRecord& record = texture_records[use.texture.index];
    const std::string_view name = name_of(record);
    if (!is_allowed(use.access, pass.kind)) {
      return PassCheck{GraphError::AccessNotAllowedForPassKind, name};
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (textures[j].texture == use.texture) {
        return PassCheck{GraphError::ResourceUsedTwiceInOnePass, name};
      }
    }
    if (use.contents != Contents::Keep && !states_contents(use.access)) {
      return PassCheck{GraphError::ContentsRequireAttachmentWrite, name};
    }
    const Shape shape = shape_of(record);
    if (shape.known && is_attachment(use.access) &&
        rhi::format_info(shape.format).is_depth != is_depth_access(use.access)) {
      return PassCheck{GraphError::AttachmentFormatMismatch, name};
    }
    if (use.access == Access::Attachment && use.slot >= rhi::kMaxRenderTargets) {
      return PassCheck{GraphError::AttachmentSlotInvalid, name};
    }
  }
  const std::span<const BufferUse> buffers = pass.buffers;
  for (std::size_t i = 0; i < buffers.size(); ++i) {
    const BufferUse& use = buffers[i];
    if (!names_resource(use.buffer, frame, buffer_records)) {
      return PassCheck{GraphError::HandleInvalid, {}};
    }
    const std::string_view name = name_of(buffer_records[use.buffer.index]);
    if (!is_allowed(use.access, pass.kind)) {
      return PassCheck{GraphError::AccessNotAllowedForPassKind, name};
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (buffers[j].buffer == use.buffer) {
        return PassCheck{GraphError::ResourceUsedTwiceInOnePass, name};
      }
    }
  }
  // The pass as a whole: at least one attachment for a Raster pass, color
  // slots packed from 0 with one attachment each, one depth attachment, one
  // extent across them all. A second depth attachment is a second use of the
  // one depth slot, so it is named as a slot error.
  std::array<bool, rhi::kMaxRenderTargets> slots{};
  std::uint32_t colors = 0;
  bool depth = false;
  Shape first{};
  for (const TextureUse& use : textures) {
    if (!is_attachment(use.access)) {
      continue;
    }
    const TextureRecord& record = texture_records[use.texture.index];
    if (is_depth_access(use.access)) {
      if (depth) {
        return PassCheck{GraphError::AttachmentSlotInvalid, name_of(record)};
      }
      depth = true;
    } else {
      if (slots[use.slot]) {
        return PassCheck{GraphError::AttachmentSlotInvalid, name_of(record)};
      }
      slots[use.slot] = true;
      ++colors;
    }
    const Shape shape = shape_of(record);
    if (!shape.known) {
      continue;
    }
    if (!first.known) {
      first = shape;
    } else if (shape.extent != first.extent) {
      return PassCheck{GraphError::AttachmentExtentMismatch, name_of(record)};
    }
  }
  if (pass.kind == PassKind::Raster && colors == 0U && !depth) {
    return PassCheck{GraphError::RasterPassWithoutAttachment, {}};
  }
  for (std::uint32_t slot = 0; slot < colors; ++slot) {
    if (!slots[slot]) {
      return PassCheck{GraphError::AttachmentSlotInvalid, {}};
    }
  }
  return PassCheck{};
}

} // namespace

void FrameGraph::mark_presented(TextureHandle texture) {
  State& s = *state_;
  if (!names_resource(texture, s.frame, s.textures)) {
    note(s.first_error, GraphError::HandleInvalid, {}, {});
    return;
  }
  TextureRecord& record = s.textures[texture.index];
  if (!record.imported) {
    note(s.first_error, GraphError::FrameOnlyCannotLeave, {}, name_of(record));
    return;
  }
  if (record.import.texture != nullptr &&
      !rhi::has_usage(record.import.texture->description().usage, rhi::TextureUsage::BackBuffer)) {
    note(s.first_error, GraphError::PresentedNotBackBuffer, {}, name_of(record));
    return;
  }
  record.presented = true;
}

void FrameGraph::mark_history(TextureHandle texture) {
  State& s = *state_;
  if (!names_resource(texture, s.frame, s.textures)) {
    note(s.first_error, GraphError::HandleInvalid, {}, {});
    return;
  }
  TextureRecord& record = s.textures[texture.index];
  if (!record.imported) {
    note(s.first_error, GraphError::FrameOnlyCannotLeave, {}, name_of(record));
    return;
  }
  record.history = true;
}

void FrameGraph::mark_history(BufferHandle buffer) {
  State& s = *state_;
  if (!names_resource(buffer, s.frame, s.buffers)) {
    note(s.first_error, GraphError::HandleInvalid, {}, {});
    return;
  }
  BufferRecord& record = s.buffers[buffer.index];
  if (!record.imported) {
    note(s.first_error, GraphError::FrameOnlyCannotLeave, {}, name_of(record));
    return;
  }
  record.history = true;
}

void FrameGraph::mark_readback(TextureHandle texture, const ReadBack& readback) {
  State& s = *state_;
  if (!names_resource(texture, s.frame, s.textures)) {
    note(s.first_error, GraphError::HandleInvalid, {}, {});
    return;
  }
  s.texture_readbacks[texture.index] = readback;
}

void FrameGraph::mark_readback(BufferHandle buffer, const ReadBack& readback) {
  State& s = *state_;
  if (!names_resource(buffer, s.frame, s.buffers)) {
    note(s.first_error, GraphError::HandleInvalid, {}, {});
    return;
  }
  s.buffer_readbacks[buffer.index] = readback;
}

// ---- What was declared ------------------------------------------------------------

std::uint32_t FrameGraph::texture_count() const noexcept {
  return static_cast<std::uint32_t>(state_->textures.size());
}

std::uint32_t FrameGraph::buffer_count() const noexcept {
  return static_cast<std::uint32_t>(state_->buffers.size());
}

std::uint32_t FrameGraph::pass_count() const noexcept {
  return static_cast<std::uint32_t>(state_->passes.size());
}

bool FrameGraph::is_imported(TextureHandle texture) const noexcept {
  return names_resource(texture, state_->frame, state_->textures) &&
         state_->textures[texture.index].imported;
}

bool FrameGraph::is_imported(BufferHandle buffer) const noexcept {
  return names_resource(buffer, state_->frame, state_->buffers) &&
         state_->buffers[buffer.index].imported;
}

TextureHandle FrameGraph::texture_handle(std::uint32_t index) const noexcept {
  if (index >= state_->textures.size()) {
    return TextureHandle{};
  }
  return TextureHandle{static_cast<std::uint16_t>(index), state_->frame};
}

BufferHandle FrameGraph::buffer_handle(std::uint32_t index) const noexcept {
  if (index >= state_->buffers.size()) {
    return BufferHandle{};
  }
  return BufferHandle{static_cast<std::uint16_t>(index), state_->frame};
}

const ImportedTexture* FrameGraph::imported_texture(TextureHandle texture) const noexcept {
  if (!names_resource(texture, state_->frame, state_->textures) ||
      !state_->textures[texture.index].imported) {
    return nullptr;
  }
  return &state_->textures[texture.index].import;
}

const ImportedBuffer* FrameGraph::imported_buffer(BufferHandle buffer) const noexcept {
  if (!names_resource(buffer, state_->frame, state_->buffers) ||
      !state_->buffers[buffer.index].imported) {
    return nullptr;
  }
  return &state_->buffers[buffer.index].import;
}

const FrameTextureDescription*
FrameGraph::texture_description(TextureHandle texture) const noexcept {
  if (!names_resource(texture, state_->frame, state_->textures) ||
      state_->textures[texture.index].imported) {
    return nullptr;
  }
  return &state_->textures[texture.index].description;
}

const FrameBufferDescription* FrameGraph::buffer_description(BufferHandle buffer) const noexcept {
  if (!names_resource(buffer, state_->frame, state_->buffers) ||
      state_->buffers[buffer.index].imported) {
    return nullptr;
  }
  return &state_->buffers[buffer.index].description;
}

std::span<const TextureUse> FrameGraph::texture_uses(PassIndex pass) const noexcept {
  if (pass >= state_->passes.size()) {
    return {};
  }
  return state_->passes[pass].textures;
}

std::span<const BufferUse> FrameGraph::buffer_uses(PassIndex pass) const noexcept {
  if (pass >= state_->passes.size()) {
    return {};
  }
  return state_->passes[pass].buffers;
}

const PassDeclaration* FrameGraph::pass(PassIndex pass) const noexcept {
  if (pass >= state_->passes.size()) {
    return nullptr;
  }
  return &state_->passes[pass];
}

std::span<const ReadBack> FrameGraph::texture_readbacks() const noexcept {
  return state_->texture_readbacks;
}

std::span<const ReadBack> FrameGraph::buffer_readbacks() const noexcept {
  return state_->buffer_readbacks;
}

// ---- Execution side ---------------------------------------------------------------

rhi::Texture* FrameGraph::texture(TextureHandle texture) const noexcept {
  if (!names_resource(texture, state_->frame, state_->textures)) {
    return nullptr;
  }
  const TextureRecord& record = state_->textures[texture.index];
  return record.imported ? record.import.texture : record.acquired.get();
}

rhi::Buffer* FrameGraph::buffer(BufferHandle buffer) const noexcept {
  if (!names_resource(buffer, state_->frame, state_->buffers)) {
    return nullptr;
  }
  const BufferRecord& record = state_->buffers[buffer.index];
  return record.imported ? record.import.buffer : record.acquired.get();
}

void FrameGraph::acquire_texture(TextureHandle texture, rhi::Ref<rhi::Texture> acquired) noexcept {
  if (names_resource(texture, state_->frame, state_->textures) &&
      !state_->textures[texture.index].imported) {
    state_->textures[texture.index].acquired = std::move(acquired);
  }
}

void FrameGraph::acquire_buffer(BufferHandle buffer, rhi::Ref<rhi::Buffer> acquired) noexcept {
  if (names_resource(buffer, state_->frame, state_->buffers) &&
      !state_->buffers[buffer.index].imported) {
    state_->buffers[buffer.index].acquired = std::move(acquired);
  }
}

void FrameGraph::release_texture(TextureHandle texture) noexcept {
  if (names_resource(texture, state_->frame, state_->textures)) {
    state_->textures[texture.index].acquired.reset();
  }
}

void FrameGraph::release_buffer(BufferHandle buffer) noexcept {
  if (names_resource(buffer, state_->frame, state_->buffers)) {
    state_->buffers[buffer.index].acquired.reset();
  }
}

} // namespace bw::graph
