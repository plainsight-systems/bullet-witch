// Material animation refinement applies both-phase R.2 (#rr-use-ptr) to
// bounded borrowed labels/keys and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) to setup-only metadata allocation.
// Matrix-reference additions use both-phase C.32 (#rc-dtor-ptr) for owned
// material associations and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) for one-time metadata construction.
// Original model CPU decoder; ownership and algorithm contract in model.hpp.
// No GPU objects or readiness publication: retained material operations still
// require an evidenced renderer consumer. Traverse original records in order,
// capture active state at draws, then reject invalid buffer/index associations.
// Cost O(metadata + referenced indices), no buffer-payload copies. Recursion is
// bounded at 32 nested records; allocation failure unwinds the complete result.
// Design and implementation: cpp-guidelines SL.con.3 (#rsl-bounds) checks
// subranges before arithmetic; R.2 (#rr-use-ptr) exposes owner-borrowed spans.
// cpp-performance MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) retains explicit allocators.
// Native evidence: 0x419320 mesh construction, 0x4303f0 record dispatch,
// 0x41aa70 index format, and stage-content-decoding.md. Noesis 4.474 supplies
// framing cross-checks only; its console vertex layout is not used on PC.
#include "bw/formats/model.hpp"

#include <bit>
#include <limits>

#include "reader.hpp"

namespace bw::formats {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
using resources::detail::unsupported;
constexpr std::size_t kMetadataOffsetField = 5;
constexpr std::size_t kMetadataSizeField = 6;
constexpr std::uint64_t kRecordHeaderBytes = 8;
constexpr std::uint64_t kChunkHeaderBytes = 12;
constexpr std::uint64_t kTextureNameBytes = 32;
constexpr std::uint64_t kTextureNameOffset = 12;
constexpr std::uint64_t kDeclarationMinimumBytes = 12;
constexpr std::uint64_t kDeclarationElementBytes = 8;
constexpr std::uint64_t kElementMethodOffset = 5;
constexpr std::uint64_t kElementUsageOffset = 6;
constexpr std::uint64_t kElementUsageIndexOffset = 7;
constexpr std::uint16_t kTerminatorStream = 255;
constexpr std::uint8_t kTerminatorType = 17;
constexpr std::uint64_t kStreamRecordBytes = 16;
constexpr std::uint64_t kStreamOffsetField = 8;
constexpr std::uint64_t kStreamStrideField = 12;
constexpr std::uint64_t kTextureReferenceBytes = 16;
constexpr std::uint64_t kTextureIndexField = 8;
constexpr std::uint64_t kTextureSecondaryField = 12;
constexpr std::uint64_t kDrawRecordBytes = 24;
constexpr std::uint64_t kMinimumVertexField = 8;
constexpr std::uint64_t kVertexCountField = 12;
constexpr std::uint64_t kStartIndexField = 16;
constexpr std::uint64_t kPrimitiveCountField = 20;
constexpr std::uint64_t kConstantRecordBytes = 32;
constexpr std::uint64_t kConstantValueOffset = 16;
constexpr std::uint32_t kMaximumDepth = 32;
constexpr std::size_t kUnset = std::numeric_limits<std::size_t>::max();
constexpr std::size_t kHeaderSize = 96;
constexpr std::size_t kChunkOffset = 92;
constexpr std::uint32_t kTriangleList = 4;
constexpr std::uint32_t kIndexWidthFlag = 0x80000000;
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t tag(std::string_view name) {
  std::uint32_t result = 0;
  for (char ch : name) {
    result = (result << kByteBits) | static_cast<unsigned char>(ch);
  }
  return result;
}
std::uint16_t half(const Reader& r, std::uint64_t offset) {
  r.range(offset, 2);
  return static_cast<std::uint16_t>(
      r.b[static_cast<std::size_t>(offset)] |
      static_cast<std::uint16_t>(r.b[static_cast<std::size_t>(offset + 1)])
          << kByteBits);
}
void exact(std::uint64_t actual, std::uint64_t expected) {
  if (actual != expected) {
    malformed("Model record has unexpected size");
  }
}
std::uint32_t element_width(std::uint8_t type) {
  constexpr std::array<std::uint32_t, 17> widths{4, 8, 12, 16, 4, 4, 4, 8, 4,
                                                 4, 8, 4,  8,  4, 4, 4, 8};
  if (type >= widths.size()) {
    unsupported("Unsupported model declaration type");
  }
  return widths.at(type);
}
// Continued command blocks overwrite a semantic while leaving all other
// device bindings intact. Material snapshots keep one effective value per key.
template <class Entry, class Key>
void assign_binding(memory::Vector<Entry>& entries, const Entry& entry,
                    Key key) {
  const auto found = std::ranges::find(entries, key(entry), key);
  if (found == entries.end()) {
    entries.push_back(entry);
  } else {
    *found = entry;
  }
}
}  // namespace

struct ModelDefinition::State {
  std::size_t declaration{kUnset}, material{kUnset}, index_buffer{kUnset};
  ModelStream stream{};
  std::optional<ModelStream> shape_stream;
  bool has_stream{};
  std::array<std::uint32_t, 2> shader_selector{};
  std::uint32_t chunk{}, group{}, volume{};
  std::array<std::optional<std::uint32_t>, 2> texture_animation_slots;
};

ModelDefinition::ModelDefinition(resources::ResourceArchive archive,
                                 const memory::Allocator<std::byte>& allocator)
    : archive_(std::move(archive)),
      allocator_(allocator),
      declarations_(allocator),
      vertex_buffers_(allocator),
      index_buffers_(allocator),
      shape_buffers_(allocator),
      shape_offsets_(allocator),
      materials_(allocator),
      draws_(allocator),
      records_(allocator),
      texture_names_(allocator),
      material_animation_names_(allocator) {}

ModelDefinition ModelDefinition::parse(resources::ResourceArchive archive,
                                       std::shared_ptr<memory::Domain> domain) {
  ModelDefinition result(std::move(archive),
                         memory::Allocator<std::byte>(std::move(domain)));
  result.decode();
  return result;
}
std::span<const std::uint8_t> ModelDefinition::header() const {
  return archive_.payload(header_record_);
}
std::span<const std::uint8_t> ModelDefinition::vertex_bytes(
    std::size_t buffer) const {
  return archive_.payload(vertex_buffers_.at(buffer).archive_record);
}
std::span<const std::uint8_t> ModelDefinition::index_bytes(
    std::size_t buffer) const {
  return archive_.payload(index_buffers_.at(buffer).archive_record);
}

std::span<const std::uint8_t> ModelDefinition::shape_bytes(
    std::size_t buffer) const {
  return archive_.payload(shape_buffers_.at(buffer).archive_record);
}

std::span<const std::uint8_t> ModelDefinition::shape_offset_bytes(
    std::size_t buffer) const {
  return archive_.payload(shape_offsets_.at(buffer).archive_record);
}

void ModelDefinition::decode() {
  // Preserve CPR record order: native mesh buffer arrays use that order.
  bool found_header = false;
  const Reader archive_reader{archive_.bytes()};
  for (std::size_t i = 0; i < archive_.records().size(); ++i) {
    const auto& record = archive_.records().at(i);
    if (record.name == "header.bin") {
      if (found_header) {
        malformed("Duplicate model header");
      }
      header_record_ = i;
      found_header = true;
    } else if (record.type == "DXVB") {
      if (record.name.starts_with("shapevertex")) {
        shape_buffers_.push_back({i, 0});
      } else {
        vertex_buffers_.push_back({i, 0});
      }
    } else if (record.type == "DXIB") {
      const auto metadata = archive_.sections().at(3);
      archive_reader.within(metadata, record.raw.at(kMetadataOffsetField),
                            record.raw.at(kMetadataSizeField));
      if (record.raw.at(kMetadataSizeField) < sizeof(std::uint32_t)) {
        malformed("Missing index format metadata");
      }
      const auto flags = archive_reader.u32(
          metadata.offset + record.raw.at(kMetadataOffsetField));
      index_buffers_.push_back({i, (flags & kIndexWidthFlag) != 0 ? 4U : 2U});
    } else if (record.name.starts_with("shapeoffset")) {
      shape_offsets_.push_back({i, 0});
    } else if (record.name == "motion._bl") {
      if (skeleton_) malformed("Duplicate model skeleton");
      skeleton_ =
          SkeletonDefinition::parse(archive_.payload(i), allocator_.domain());
    } else if (record.name == "texlist.bin" || record.name == "anim_mat.bin") {
      const Reader names{archive_.payload(i)};
      if (names.tag_view(0) != "TLST") {
        malformed("Invalid model texture table");
      }
      const auto count = names.u32(4);
      constexpr std::uint64_t kNameStride = 36;
      exact(names.b.size(), kRecordHeaderBytes + kNameStride * count);
      for (std::uint32_t n = 0; n < count; ++n) {
        auto& destination = record.name == "texlist.bin"
                                ? texture_names_
                                : material_animation_names_;
        destination.emplace_back(
            names.text_view(
                {kTextureNameOffset + kNameStride * n, kTextureNameBytes}, 0),
            allocator_);
      }
    }
  }
  if (!found_header) {
    malformed("Missing model header.bin");
  }
  const Reader r{header()};
  r.range(0, kHeaderSize);
  if (r.u32(0) != tag("VFX2")) {
    unsupported("Unsupported model header signature");
  }
  const auto relative = r.u32(kChunkOffset);
  if (relative < kChunkHeaderBytes) {
    malformed("Invalid model chunk offset");
  }
  std::uint64_t cursor =
      kHeaderSize + static_cast<std::uint64_t>(relative) - kChunkHeaderBytes;
  r.range(cursor, 0);
  while (cursor < r.b.size()) {
    r.range(cursor, kChunkHeaderBytes);
    if (r.u32(cursor + 4) != tag("CNKH")) {
      unsupported("Unsupported model trailing chunk");
    }
    const auto size = r.u32(cursor + kRecordHeaderBytes);
    r.range(cursor + kChunkHeaderBytes, size);
    State state;
    state.chunk = r.u32(cursor);
    records({cursor + kChunkHeaderBytes, size}, state, 0);
    cursor += kChunkHeaderBytes + static_cast<std::uint64_t>(size);
  }
  for (const auto& draw : draws_) {
    validate_draw(draw);
  }
}

void ModelDefinition::records(resources::Range range, State& state,
                              std::uint32_t depth) {
  if (depth > kMaximumDepth) {
    malformed("Model nesting exceeds decoder bound");
  }
  const Reader r{header()};
  r.range(range.offset, range.size);
  std::uint64_t cursor = 0;
  while (cursor < range.size) {
    r.within(range, cursor, kRecordHeaderBytes);
    const auto kind = r.u32(range.offset + cursor);
    const std::uint64_t size = r.u32(range.offset + cursor + 4);
    r.within(range, cursor + kRecordHeaderBytes, size);
    const resources::Range payload{range.offset + cursor + kRecordHeaderBytes,
                                   size};
    const auto p = payload.offset;
    records_.push_back({kind, depth, payload});
    // Metadata/state records remain ordered even when their GPU semantics await
    // reconstruction.
    switch (kind) {
      case tag("VDCL"): {
        if (size < kDeclarationMinimumBytes) {
          malformed("Truncated model vertex declaration");
        }
        declarations_.emplace_back(allocator_);
        auto& declaration = declarations_.back();
        bool terminated = false;
        for (std::uint64_t q = 4; q + kDeclarationElementBytes <= size;
             q += kDeclarationElementBytes) {
          ModelVertexElement element{
              half(r, p + q),
              half(r, p + q + 2),
              r.b[static_cast<std::size_t>(p + q + 4)],
              r.b[static_cast<std::size_t>(p + q + kElementMethodOffset)],
              r.b[static_cast<std::size_t>(p + q + kElementUsageOffset)],
              r.b[static_cast<std::size_t>(p + q + kElementUsageIndexOffset)]};
          if (element.stream == kTerminatorStream &&
              element.type == kTerminatorType) {
            terminated = true;
            break;
          }
          (void)element_width(element.type);
          if (element.stream > 1 || element.method != 0) {
            unsupported("Model declaration needs additional stream/method");
          }
          declaration.elements.push_back(element);
        }
        if (!terminated || declaration.elements.empty()) {
          malformed("Invalid model vertex declaration terminator");
        }
        state.declaration = declarations_.size() - 1;
        break;
      }
      case tag("VTX "):
        exact(size, kStreamRecordBytes);
        state.stream = {r.u32(p), r.u32(p + 4), r.u32(p + kStreamOffsetField),
                        r.u32(p + kStreamStrideField)};
        state.has_stream = true;
        break;
      case tag("VTXS"):
        exact(size, kStreamRecordBytes);
        state.shape_stream =
            ModelStream{r.u32(p), r.u32(p + 4), r.u32(p + kStreamOffsetField),
                        r.u32(p + kStreamStrideField)};
        break;
      case tag("IDX "):
        exact(size, 4);
        state.index_buffer = r.u32(p);
        break;
      case tag("TANM"): {
        exact(size, kRecordHeaderBytes);
        const auto destination = r.u32(p);
        if (destination >= state.texture_animation_slots.size() ||
            r.u32(p + 4) >= material_animation_names_.size()) {
          unsupported("Unresolved model texture animation slot");
        }
        state.texture_animation_slots.at(destination) = r.u32(p + 4);
        break;
      }
      case tag("TRST"): {
        exact(size, sizeof(std::uint32_t));
        const auto destination = r.u32(p);
        if (destination >= state.texture_animation_slots.size()) {
          unsupported("Unresolved model texture animation reset");
        }
        state.texture_animation_slots.at(destination).reset();
        break;
      }
      case tag("knsS"):
        exact(size, kRecordHeaderBytes);
        state.shader_selector = {r.u32(p), r.u32(p + 4)};
        break;
      case tag("knsC"):
        // Continuations execute into the same native material/device state.
        // Capture the inherited bindings before appending (which may relocate
        // the vector), then replace the per-draw joint palette operations.
        if (state.material == kUnset) {
          materials_.emplace_back(allocator_);
        } else {
          auto inherited = materials_.at(state.material);
          inherited.matrices.clear();
          materials_.push_back(std::move(inherited));
        }
        state.material = materials_.size() - 1;
        materials_.back().shader_selector = state.shader_selector;
        materials_.back().records = payload;
        records(payload, state, depth + 1);
        break;
      case tag("kns2"):
        materials_.emplace_back(allocator_);
        state.material = materials_.size() - 1;
        materials_.back().shader_selector = state.shader_selector;
        materials_.back().records = payload;
        records(payload, state, depth + 1);
        break;
      case tag("MCTL"):
        if (size < kRecordHeaderBytes) {
          malformed("Truncated model material control");
        }
        records({p + kRecordHeaderBytes, size - kRecordHeaderBytes}, state,
                depth + 1);
        break;
      case tag("TR  "): {
        if (state.material == kUnset || size < kRecordHeaderBytes) {
          malformed("Texture references without material");
        }
        const auto count = r.u32(p + 4);
        exact(size, kRecordHeaderBytes + static_cast<std::uint64_t>(count) *
                                             kTextureReferenceBytes);
        for (std::uint32_t i = 0; i < count; ++i) {
          const auto q = p + kRecordHeaderBytes +
                         static_cast<std::uint64_t>(i) * kTextureReferenceBytes;
          ModelTextureReference ref{{r.u32(q), r.u32(q + 4)},
                                    r.u32(q + kTextureIndexField),
                                    r.u32(q + kTextureSecondaryField)};
          assign_binding(materials_.at(state.material).textures, ref,
                         [](const auto& value) { return value.semantic[0]; });
        }
        break;
      }
      case tag("DGRP"):
      case tag("DVOL"): {
        if (size < 4) {
          malformed("Truncated model group");
        }
        // Nesting changes selection identity, while device binding state
        // remains sequential.
        const auto old_group = state.group;
        const auto old_volume = state.volume;
        if (kind == tag("DGRP")) {
          state.group = r.u32(p);
        } else {
          state.volume = r.u32(p);
        }
        records({p + 4, size - 4}, state, depth + 1);
        state.group = old_group;
        state.volume = old_volume;
        break;
      }
      case tag("CNKH"):
        records(payload, state, depth + 1);
        break;
      case tag("DIP "):
        exact(size, kDrawRecordBytes);
        if (!state.has_stream || state.declaration == kUnset ||
            state.material == kUnset || state.index_buffer == kUnset) {
          malformed("Model draw precedes required bindings");
        }
        draws_.push_back({r.u32(p),
                          std::bit_cast<std::int32_t>(r.u32(p + 4)),
                          r.u32(p + kMinimumVertexField),
                          r.u32(p + kVertexCountField),
                          r.u32(p + kStartIndexField),
                          r.u32(p + kPrimitiveCountField),
                          state.declaration,
                          state.material,
                          state.index_buffer,
                          state.stream,
                          state.chunk,
                          state.group,
                          state.volume,
                          {},
                          state.texture_animation_slots});
        if (std::ranges::any_of(
                declarations_.at(state.declaration).elements,
                [](const auto& element) { return element.stream == 1; })) {
          if (!state.shape_stream) {
            malformed("Shape declaration has no stream binding");
          }
          draws_.back().shape_stream = state.shape_stream;
        }
        break;
      case tag("CF1I"): {
        if (state.material == kUnset || size < kRecordHeaderBytes) {
          malformed("Constants without material");
        }
        const auto count = r.u32(p + 4);
        exact(size, kRecordHeaderBytes + static_cast<std::uint64_t>(count) *
                                             kConstantRecordBytes);
        for (std::uint32_t i = 0; i < count; ++i) {
          const auto q = p + kRecordHeaderBytes +
                         static_cast<std::uint64_t>(i) * kConstantRecordBytes;
          ModelConstant constant{{r.u32(q), r.u32(q + 4)},
                                 {r.u32(q + kTextureIndexField),
                                  r.u32(q + kTextureSecondaryField)},
                                 {}};
          for (std::size_t component = 0; component < constant.value.size();
               ++component) {
            constant.value.at(component) = std::bit_cast<float>(
                r.u32(q + kConstantValueOffset + component * 4));
          }
          assign_binding(materials_.at(state.material).constants, constant,
                         [](const auto& value) { return value.semantic[0]; });
        }
        break;
      }
      case tag("SRS "):
      case tag("SSSM"): {
        if (state.material == kUnset || size < kRecordHeaderBytes) {
          malformed("State entries without material");
        }
        constexpr std::uint64_t kStateEntryBytes = 16;
        constexpr std::uint64_t kStateValueOffset = 8;
        constexpr std::uint64_t kStateAuxiliaryOffset = 12;
        const auto count = r.u32(p + 4);
        exact(size, kRecordHeaderBytes + count * kStateEntryBytes);
        auto& material = materials_.at(state.material);
        auto& entries = kind == tag("SRS ") ? material.render_states
                                            : material.sampler_states;
        for (std::uint32_t i = 0; i < count; ++i) {
          const auto q = p + kRecordHeaderBytes + i * kStateEntryBytes;
          entries.push_back({r.u32(q), r.u32(q + 4),
                             r.u32(q + kStateValueOffset),
                             r.u32(q + kStateAuxiliaryOffset)});
        }
        break;
      }
      case tag("CM1R"): {
        // Preserve source joints and destination shader slots independently;
        // the scene owner supplies the actual pose before submission.
        if (state.material == kUnset || size < kRecordHeaderBytes) {
          malformed("Truncated matrix reference record");
        }
        constexpr std::uint64_t kMatrixReferenceBytes = 16;
        const auto count = r.u32(p + 4);
        exact(size, kRecordHeaderBytes + static_cast<std::uint64_t>(count) *
                                             kMatrixReferenceBytes);
        for (std::uint32_t index = 0; index < count; ++index) {
          const auto entry =
              p + kRecordHeaderBytes + index * kMatrixReferenceBytes;
          constexpr std::uint64_t kSourceOffset = 8, kDestinationOffset = 12;
          materials_.at(state.material)
              .matrices.push_back({r.u32(entry), r.u32(entry + kSourceOffset),
                                   r.u32(entry + kDestinationOffset)});
        }
        break;
      }
      case tag("GENV"):
        break;  // Retained raw; the renderer must interpret or reject its
                // required semantics.
      default:
        unsupported("Unsupported model record tag " + std::to_string(kind));
    }
    cursor += kRecordHeaderBytes + size;
  }
}

void ModelDefinition::validate_draw(const ModelDraw& draw) const {
  if (draw.stream.stream != 0 || draw.primitive_type != kTriangleList) {
    unsupported("Model draw requires additional stream/primitive support");
  }
  if (draw.stream.buffer >= vertex_buffers_.size() ||
      draw.index_buffer >= index_buffers_.size()) {
    malformed("Model draw references missing buffer");
  }
  const auto first_vertex =
      static_cast<std::int64_t>(draw.base_vertex) + draw.minimum_vertex;
  const auto validate_stream = [&](const ModelStream& stream,
                                   std::span<const std::uint8_t> vertices) {
    if (stream.stride == 0 || stream.offset > vertices.size()) {
      malformed("Invalid model vertex stride/offset");
    }
    const auto available = (vertices.size() - stream.offset) / stream.stride;
    for (const auto& element : declarations_.at(draw.declaration).elements) {
      if (element.stream != stream.stream) continue;
      if (element.offset > stream.stride ||
          element_width(element.type) > stream.stride - element.offset) {
        malformed("Vertex element exceeds model stream stride");
      }
    }
    if (first_vertex < 0 ||
        static_cast<std::uint64_t>(first_vertex) > available ||
        draw.vertex_count >
            available - static_cast<std::uint64_t>(first_vertex)) {
      malformed("Model declared vertex range exceeds stream");
    }
  };
  validate_stream(draw.stream, vertex_bytes(draw.stream.buffer));
  if (draw.shape_stream) {
    if (draw.shape_stream->stream != 1 ||
        draw.shape_stream->buffer >= shape_buffers_.size()) {
      malformed("Model draw references missing shape buffer");
    }
    validate_stream(*draw.shape_stream, shape_bytes(draw.shape_stream->buffer));
  }
  const Reader indices{index_bytes(draw.index_buffer)};
  const auto width = index_buffers_.at(draw.index_buffer).index_element_bytes;
  const auto count = static_cast<std::uint64_t>(draw.primitive_count) * 3;
  indices.range(static_cast<std::uint64_t>(draw.start_index) * width,
                count * width);
  // Check actual references as well as the declared draw range before upload.
  for (std::uint64_t i = 0; i < count; ++i) {
    const auto position =
        (static_cast<std::uint64_t>(draw.start_index) + i) * width;
    const auto index =
        width == 2 ? half(indices, position) : indices.u32(position);
    if (index < draw.minimum_vertex ||
        static_cast<std::uint64_t>(index) - draw.minimum_vertex >=
            draw.vertex_count) {
      malformed("Model index escapes declared draw vertex range");
    }
  }
}
}  // namespace bw::formats
