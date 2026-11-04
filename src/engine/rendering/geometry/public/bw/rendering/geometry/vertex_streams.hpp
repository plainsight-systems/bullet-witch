#ifndef BW_RENDERING_GEOMETRY_VERTEX_STREAMS_HPP
#define BW_RENDERING_GEOMETRY_VERTEX_STREAMS_HPP
#include "bw/rendering/shaders/parameter_block.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

// Responsibility: vertex pulling with no input layout
// (docs/architecture/shaders.md, invariant 5 and "Draw-time binding": a
// vertex shader reaches its buffers by index into the typed descriptor
// arrays; docs/architecture/materials.md, "Material to pipelines": the
// vertex layout enters the pipeline description, not the permutation). A
// mesh is one or two vertex streams, each a byte-addressed buffer with a
// stride, and a set of attributes at byte offsets in those streams. The
// draw block carries the streams' descriptor indices, strides, vertex
// counts, the draw's base vertex and the attribute table; the vertex shader
// decodes each attribute with one function per element type
// (src/engine/rendering/shaders/hlsl/bw/vertex_fetch.hlsli). Index buffers
// stay hardware index buffers, 16-bit, bound with command_list.hpp.
//
// Contract.
//   Element types: exactly the five the content uses: float3, short4n,
//   half2, ubyte4, ubyte4n (the content's 38 vertex declarations use only
//   these five types). element_bytes gives 12, 8, 4, 4, 4.
//   Attributes: one slot per attribute the content names (VertexAttribute),
//   each at most once in a layout; the slot numbers are the shader's
//   (vertex_fetch.hlsli, BW_ATTRIBUTE_*), and a change on either side is a
//   decode-test failure. A layout names which slots exist; an absent slot
//   reads kAbsentAttribute in the block.
//   Streams: at most kMaxVertexStreams (2) per layout, each with a byte
//   stride; attribute offsets and strides are multiples of
//   kAttributeAlignment (4), the byte-address alignment every target
//   requires of a raw buffer load; an attribute ends within its stream's
//   stride; every declared stream carries at least one attribute; an
//   offset fits the 16 bits the attribute word gives it.
//   Vertex index: every draw passes base vertex 0 and first instance 0 to
//   the API and carries the draw's base vertex in the block; the shader
//   reads vertex SV_VertexID + base_vertex. This makes the same HLSL read
//   the same vertex on every target whatever the API's base-vertex
//   semantics, and lets 16-bit indices address a group of more than 65536
//   vertices through per-draw bases.
//   Bounds: the shader does no bounds check; a draw whose range runs past a
//   stream buffer is refused before recording, by validate_vertex_range,
//   which whoever fills the block calls (in debug builds at least). The
//   validation layer does not perform this check: it sees the draw block as
//   bytes and a layout hash, and cannot locate a VertexStreams block nested
//   in an arbitrary draw block; stating that here is the plan's one
//   departure in this header.
//   The VertexStreams block: a parameter block nested in a renderer's draw
//   block; its layout is the shader system's (parameter_block.hpp) and the
//   HLSL reads the same bytes by construction. make_vertex_streams fills it
//   from a validated layout, the streams' buffer indices and counts, and
//   the base vertex.
//
// The APIs differ on the base vertex: Metal's vertex_id and Vulkan's
// VertexIndex include it, and D3D12 does not state whether SV_VertexID in an
// indexed draw does; carrying it in the block removes the difference.
// Shader buffer reads are not bounds checked unless robustness is enabled;
// robustness is not relied on: the caller checks the range with
// validate_vertex_range before it issues the draw, on every target.
// Pulled indices buy nothing until a culling pass writes index buffers, so
// this slice keeps hardware index buffers and a per-draw base vertex. The
// base is per draw, not per instance: the content has no per-instance
// stream and every draw is one mesh.
//
// Core guidelines read: I.1 (the layout, buffers and counts are explicit
// values; the shader gets them through the block), E.27 and SL.con.3 (a
// named error for every out-of-range input, checked before the GPU reads),
// Con.5 (every check is constexpr), Enum.3. Performance guidelines read:
// GPU.2 (interleaved streams keep a vertex's attributes in adjacent
// addresses; the content's declarations stay as authored), CDSA.32 (the
// importer transforms content once into these streams; this header is the
// consumer's layout), GDSA.12 (buffers reached by a direct index).
namespace bw::geometry {

enum class VertexElementType : std::uint8_t {
  Float3,
  Short4Norm,
  Half2,
  UByte4,
  UByte4Norm,
};
inline constexpr std::uint32_t kVertexElementTypeCount = 5;

[[nodiscard]] constexpr std::uint32_t element_bytes(VertexElementType type) noexcept {
  switch (type) {
  case VertexElementType::Float3:
    return 12;
  case VertexElementType::Short4Norm:
    return 8;
  case VertexElementType::Half2:
  case VertexElementType::UByte4:
  case VertexElementType::UByte4Norm:
    return 4;
  }
  return 0;
}

// The attribute slots, one per attribute the content names: position,
// normal, tangent, three color sets, texture coordinate sets 1 to 4 (the
// content has no set 0), bone slots and bone weights. The numbers are the
// shader's (vertex_fetch.hlsli).
enum class VertexAttribute : std::uint8_t {
  Position = 0,
  Normal = 1,
  Tangent = 2,
  Color0 = 3,
  Color1 = 4,
  Color2 = 5,
  TexCoord1 = 6,
  TexCoord2 = 7,
  TexCoord3 = 8,
  TexCoord4 = 9,
  BoneSlots = 10,
  BoneWeights = 11,
};
inline constexpr std::uint32_t kVertexAttributeCount = 12;
inline constexpr std::uint32_t kMaxVertexStreams = 2;
inline constexpr std::uint32_t kAttributeAlignment = 4;
inline constexpr std::uint32_t kMaxAttributeOffset = 0xFFFF;
// The attribute word of a slot the layout does not name.
inline constexpr std::uint32_t kAbsentAttribute = 0xFFFFFFFFU;

struct VertexAttributeDescription {
  VertexAttribute attribute{VertexAttribute::Position};
  VertexElementType type{VertexElementType::Float3};
  std::uint8_t stream{};
  std::uint32_t offset{};
  [[nodiscard]] friend constexpr bool operator==(VertexAttributeDescription,
                                                 VertexAttributeDescription) noexcept = default;
};
struct VertexStreamDescription {
  std::uint32_t stride{};
  [[nodiscard]] friend constexpr bool operator==(VertexStreamDescription,
                                                 VertexStreamDescription) noexcept = default;
};

// A mesh's layout: its attributes and its streams. Value type; validated by
// validate_vertex_stream_layout before use.
struct VertexStreamLayout {
  std::array<VertexAttributeDescription, kVertexAttributeCount> attributes{};
  std::array<VertexStreamDescription, kMaxVertexStreams> streams{};
  std::uint8_t attribute_count{};
  std::uint8_t stream_count{};
  [[nodiscard]] friend constexpr bool operator==(const VertexStreamLayout&,
                                                 const VertexStreamLayout&) noexcept = default;
};

// Named failures, the first in this order that applies.
enum class VertexStreamError : std::uint8_t {
  None,
  NoAttributes,
  TooManyAttributes,
  NoStreams,
  TooManyStreams,
  // Two attributes name the same slot.
  DuplicateAttribute,
  // An attribute names a stream at or past stream_count.
  StreamOutOfRange,
  // A stride that is zero or not a multiple of kAttributeAlignment.
  StrideMisaligned,
  // An offset that is not a multiple of kAttributeAlignment.
  OffsetMisaligned,
  // An offset above kMaxAttributeOffset.
  OffsetOutOfRange,
  // offset + element_bytes(type) exceeds the stream's stride.
  AttributeOverrunsStride,
  // A declared stream no attribute reads.
  StreamUnused,
  // validate_vertex_range: (base_vertex + vertex_count) * stride exceeds a
  // stream's buffer bytes, or the count is zero.
  VertexRangeOverrunsBuffer,
};

[[nodiscard]] constexpr VertexStreamError
validate_vertex_stream_layout(const VertexStreamLayout& layout) noexcept {
  if (layout.attribute_count == 0U) {
    return VertexStreamError::NoAttributes;
  }
  if (layout.attribute_count > kVertexAttributeCount) {
    return VertexStreamError::TooManyAttributes;
  }
  if (layout.stream_count == 0U) {
    return VertexStreamError::NoStreams;
  }
  if (layout.stream_count > kMaxVertexStreams) {
    return VertexStreamError::TooManyStreams;
  }
  std::array<bool, kVertexAttributeCount> seen{};
  std::array<bool, kMaxVertexStreams> used{};
  for (std::size_t i = 0; i < layout.attribute_count; ++i) {
    const VertexAttributeDescription& a = layout.attributes[i];
    const auto slot = static_cast<std::size_t>(a.attribute);
    if (slot >= kVertexAttributeCount || seen[slot]) {
      return VertexStreamError::DuplicateAttribute;
    }
    seen[slot] = true;
    if (a.stream >= layout.stream_count) {
      return VertexStreamError::StreamOutOfRange;
    }
    const std::uint32_t stride = layout.streams[a.stream].stride;
    if (stride == 0U || stride % kAttributeAlignment != 0U) {
      return VertexStreamError::StrideMisaligned;
    }
    if (a.offset % kAttributeAlignment != 0U) {
      return VertexStreamError::OffsetMisaligned;
    }
    if (a.offset > kMaxAttributeOffset) {
      return VertexStreamError::OffsetOutOfRange;
    }
    if (a.offset + element_bytes(a.type) > stride) {
      return VertexStreamError::AttributeOverrunsStride;
    }
    used[a.stream] = true;
  }
  for (std::size_t s = 0; s < layout.stream_count; ++s) {
    if (!used[s]) {
      return VertexStreamError::StreamUnused;
    }
  }
  return VertexStreamError::None;
}

// The draw's vertex range against the streams' buffer bytes: for every
// declared stream s, (base_vertex + vertex_count) * stride(s) <=
// stream_bytes[s]; vertex_count is at least 1. stream_bytes holds one entry
// per declared stream (the view's size, resources.hpp BufferViewDescription).
// Precondition: the layout validated.
[[nodiscard]] constexpr VertexStreamError
validate_vertex_range(const VertexStreamLayout& layout, std::span<const std::uint64_t> stream_bytes,
                      std::uint32_t base_vertex, std::uint32_t vertex_count) noexcept {
  if (vertex_count == 0U || stream_bytes.size() < layout.stream_count) {
    return VertexStreamError::VertexRangeOverrunsBuffer;
  }
  const std::uint64_t end = static_cast<std::uint64_t>(base_vertex) + vertex_count;
  for (std::size_t s = 0; s < layout.stream_count; ++s) {
    if (end * layout.streams[s].stride > stream_bytes[s]) {
      return VertexStreamError::VertexRangeOverrunsBuffer;
    }
  }
  return VertexStreamError::None;
}

// The attribute word the block carries per slot: the byte offset in the low
// 16 bits, the stream in the high 16.
[[nodiscard]] constexpr std::uint32_t attribute_word(VertexAttributeDescription a) noexcept {
  return (static_cast<std::uint32_t>(a.stream) << 16U) | (a.offset & kMaxAttributeOffset);
}

// The block a draw block nests. Offsets: buffer0 0, stride0 4, count0 8,
// base_vertex 12, buffer1 16, stride1 20, count1 24, stream_count 28,
// attributes 32 (three uint4 rows, slot i at row i / 4 component i % 4);
// size 80. A layout with one stream leaves buffer1, stride1 and count1 zero.
struct alignas(shaders::kBlockAlignment) VertexStreams {
  shaders::BufferIndex buffer0{};
  std::uint32_t stride0{};
  std::uint32_t count0{};
  std::uint32_t base_vertex{};
  shaders::BufferIndex buffer1{};
  std::uint32_t stride1{};
  std::uint32_t count1{};
  std::uint32_t stream_count{};
  shaders::Array<shaders::Uint4, 3> attributes{};
};
inline constexpr std::uint32_t kVertexStreamsBytes = 80;
static_assert(sizeof(VertexStreams) == kVertexStreamsBytes);

// Per declared stream: its buffer's descriptor index and the vertices the
// buffer holds.
struct VertexStreamBinding {
  shaders::BufferIndex buffer{};
  std::uint32_t vertex_count{};
};

// Fills the block from a validated layout: every slot the layout names gets
// its attribute word, every other slot kAbsentAttribute. bindings holds one
// entry per declared stream. Precondition: the layout validated.
[[nodiscard]] constexpr VertexStreams
make_vertex_streams(const VertexStreamLayout& layout, std::span<const VertexStreamBinding> bindings,
                    std::uint32_t base_vertex) noexcept {
  VertexStreams block{};
  for (std::size_t row = 0; row < 3; ++row) {
    block.attributes[row] =
        shaders::Uint4{kAbsentAttribute, kAbsentAttribute, kAbsentAttribute, kAbsentAttribute};
  }
  for (std::size_t i = 0; i < layout.attribute_count; ++i) {
    const VertexAttributeDescription& a = layout.attributes[i];
    const auto slot = static_cast<std::size_t>(a.attribute);
    shaders::Uint4& row = block.attributes[slot / 4U];
    const std::uint32_t word = attribute_word(a);
    switch (slot % 4U) {
    case 0:
      row.x = word;
      break;
    case 1:
      row.y = word;
      break;
    case 2:
      row.z = word;
      break;
    default:
      row.w = word;
      break;
    }
  }
  block.stream_count = layout.stream_count;
  block.base_vertex = base_vertex;
  if (layout.stream_count > 0U && !bindings.empty()) {
    block.buffer0 = bindings[0].buffer;
    block.stride0 = layout.streams[0].stride;
    block.count0 = bindings[0].vertex_count;
  }
  if (layout.stream_count > 1U && bindings.size() > 1U) {
    block.buffer1 = bindings[1].buffer;
    block.stride1 = layout.streams[1].stride;
    block.count1 = bindings[1].vertex_count;
  }
  return block;
}

} // namespace bw::geometry

// The block layout macro stringizes its argument as the HLSL struct name,
// so the block is named unqualified at global scope, as every block is.
using VertexStreams = bw::geometry::VertexStreams;
BW_PARAMETER_BLOCK_LAYOUT(VertexStreams, BW_PARAMETER_MEMBER(VertexStreams, buffer0),
                          BW_PARAMETER_MEMBER(VertexStreams, stride0),
                          BW_PARAMETER_MEMBER(VertexStreams, count0),
                          BW_PARAMETER_MEMBER(VertexStreams, base_vertex),
                          BW_PARAMETER_MEMBER(VertexStreams, buffer1),
                          BW_PARAMETER_MEMBER(VertexStreams, stride1),
                          BW_PARAMETER_MEMBER(VertexStreams, count1),
                          BW_PARAMETER_MEMBER(VertexStreams, stream_count),
                          BW_PARAMETER_MEMBER(VertexStreams, attributes));
#endif
