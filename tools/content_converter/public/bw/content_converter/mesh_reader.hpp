#ifndef BW_CONTENT_CONVERTER_MESH_READER_HPP
#define BW_CONTENT_CONVERTER_MESH_READER_HPP
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: reading a model container's `header.bin`, buffers and
// morph records into mesh data, material blocks and morph sets (the
// converter's mesh reader; docs/architecture/content.md, invariant 1).
//
// Framing: a chunk is a 4-byte tag stored byte-reversed, a u32 size that
// excludes those 8 bytes, then the data. The header's top level is a
// count n at +0x58, n u32 words, then n entries of u32 index, "CNKH", u32
// size and data; the chain ends exactly at the end of the payload. Inside,
// containers hold chunks: CNKH, DGRP, DVOL, kns2, knsC, MCTL; every other
// tag is a leaf. In document order each VDCL begins a group; VTXS (stream
// 1), VTX (stream 0) and IDX attach to the current group; each kns2 or
// knsC whose MCTL holds a CF1I begins a material, numbered mesh-wide in
// order; each DIP is a draw of the current group under the current
// material, the most recent material mesh-wide (no shipped draw precedes
// its group's first material, so the data does not separate this from a
// per-group reset); a CM1R applies to the next DIP inside the same
// innermost DVOL or DGRP (7,768 of 7,768 maps; 11 DIPs sit directly under
// a DGRP). Container framing: DGRP and DVOL carry one leading u32 and MCTL
// two zero u32 before their children (every one of the 857 headers).
// Buffer record names carry three digits (vertex_000). Ten files nest a
// VDCL inside DGRP or DVOL; the walk is by document order, so they read
// the same.
//   VDCL data: u32 0, then 8-byte elements (stream u16, offset u16, type
//   u8, method u8, usage u8, usage index u8), ended by stream 0xFF type 17.
//   Types 2, 5, 8, 10, 15 are float3, ubyte4, ubyte4n, short4n, half2;
//   usages 0 position, 1/0 bone slots, 1/1 bone weights, 3 normal, 5/n
//   texture coordinate n (1..4), 6 tangent, 10/n color n (0..2). Anything
//   else is DeclarationUnsupported, by name. The content numbers its
//   texture coordinates from 1; usage 5/n is the mesh's TexCoord(n - 1),
//   so 5/1 is TexCoord0.
//   VTX/VTXS data: u32 stream, buffer number, 0, stride; the buffer is the
//   DXVB record "vertex_NNN.bin" or "shapevertex_NNN.bin"; vertex count is
//   payload bytes over stride, exact. IDX data: u32 buffer number, the
//   DXIB record "index_NNN.bin", 16-bit indices.
//   DIP: six u32: primitive type (4, triangle list, the only value),
//   base vertex, min index, vertex count, first index, primitive count;
//   the draw's index_count is 3 * primitive count.
//   CM1R: u32 0, u32 count, then 16-byte entries "TNIJ", 0, skeleton bone
//   position, slot (equal to the entry's position).
//   Bounds: four float4 at +0x10: max, min, center, radius (x of the last).
//   Material block (kept raw for material_mapping.hpp): knsS two u32;
//   CF1I 9 records of tag and four floats, in the order PARF CCOL FRSA
//   FRSB FRSR LUMI REFP SPCC ENVP; TR count × (tag, 0, index, 0); SSSM one
//   LENV triple; SRS one triple when present.
//   TS2V ("shapeoffset_NNN.bin"): "TS2V", 0x11223344, version, vertex
//   count at +0x0c, shape count at +0x10, entry table offset at +0x48,
//   data offset at +0x4c; 16-byte entries with a name offset counted from
//   the record start; 32-byte names; deltas shape-major, 8 bytes per
//   vertex. NNN pairs the set with
//   the group whose VTXS names shapevertex_NNN.
//
// Contract.
//   read_mesh_container fills MeshSource from a parsed container: every
//   group with its layout (validated by validate_vertex_stream_layout),
//   buffers copied out, draws with their material and bone map indices,
//   materials as raw blocks, bone maps, bounds, morph sets. A
//   failure is returned as a MeshReadError with the chunk offset in
//   the header payload. A material without its own knsS takes the nearest
//   preceding knsS. A mesh with no CF1I material has material_count 0 and
//   every draw kNoMaterial. The skeleton id is set by the caller
//   (conversion.hpp), not here.
//   A header chunk the reader does not read is recorded by tag in
//   `unread_chunks`. TLST (texlist.bin) is not read: texture slots resolve
//   through the sibling default.cpr. The USER records (anim_mat.bin,
//   volumeclip.bin) and the per-group slot bytes are records of the
//   container, left where they are and not visited.
//
// Core guidelines read: SL.con.3, C.183, E.27 (as cpr_container.hpp), F.1
// (one function per chunk kind). Performance guidelines read: CDSA.32
// (buffers are copied once into the cooked layout).
namespace bw::content_converter {

inline constexpr std::array<char, 4> kModelMagic{'2', 'X', 'F', 'V'};

struct Chunk {
  std::array<char, 4> tag{};
  // Offset of the tag within the header payload.
  std::uint32_t offset{};
  std::span<const std::byte> data{};
  std::uint32_t depth{};
};

// The chunk tags that hold chunks.
[[nodiscard]] bool is_container_tag(std::array<char, 4> tag) noexcept;

enum class MeshReadError : std::uint8_t {
  None,
  HeaderRecordMissing,
  BadModelMagic,
  ChunkOverrun,
  DeclarationUnsupported,
  LayoutInvalid,
  BufferMissing,
  BufferSizeNotMultipleOfStride,
  IndexBufferSizeInvalid,
  DrawOutsideGroup,
  DrawRangeInvalid,
  PrimitiveTypeUnsupported,
  BoneMapMalformed,
  MaterialBlockMalformed,
  MorphSetMalformed,
  MorphSetGroupMissing,
};

struct MeshReadFailure {
  MeshReadError error{MeshReadError::None};
  std::uint32_t offset{};
  std::array<char, 4> tag{};
  [[nodiscard]] bool ok() const noexcept { return error == MeshReadError::None; }
};

struct Cf1iRecord {
  std::array<char, 4> tag{};
  std::array<float, 4> value{};
};
struct TrSlot {
  std::array<char, 4> tag{};
  std::uint32_t index{};
};
struct MaterialBlock {
  std::uint32_t knss_feature{};
  std::uint32_t knss_second{};
  std::array<Cf1iRecord, 9> cf1i{};
  std::vector<TrSlot> slots{};
  bool has_sssm{};
  std::array<std::uint32_t, 3> sssm_lenv{};
  bool has_srs{};
  std::array<char, 4> srs_tag{};
  std::array<std::uint32_t, 3> srs_value{};
};

struct MeshSource {
  MeshAssetData mesh{};
  std::vector<MaterialBlock> materials{};
  std::vector<MorphTargetsData> morph_sets{};
  // Tags seen and not read, each once, in first-seen order.
  std::vector<std::array<char, 4>> unread_chunks{};
};

// The chunk walk alone, document order, for tests and for readers of other
// chunk-framed records.
[[nodiscard]] MeshReadFailure walk_chunks(std::span<const std::byte> header_payload,
                                          std::vector<Chunk>& out);

[[nodiscard]] MeshReadFailure read_mesh_container(const CprContainer& container, MeshSource& out);

} // namespace bw::content_converter

#endif
