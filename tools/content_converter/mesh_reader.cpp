#include "bw/content_converter/mesh_reader.hpp"

#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/morph_targets_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"

#include "bytes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Responsibility: the out-of-line part of bw/content_converter/mesh_reader.hpp:
// the chunk walk of a model header and the reading of its groups, buffers,
// draws, material blocks, bone maps and morph sets.
//
// Contract (the header's): read_mesh_container names the first failure in
// MeshReadError order. The walk runs first and alone (it names
// BadModelMagic and ChunkOverrun); one document-order pass then pairs every
// chunk with its group, material and bone map without judging it; then each
// later check is a pass over every group, draw, map, material or morph set
// before the next check runs, so a file that breaks two rules reports the
// one earlier in the enumeration. Buffers are copied out of the container.
//
// Framing the header leaves implicit, observed over all 857 model headers
// (every container's children tile it exactly under these rules, with no
// failure in any file):
//   Before its children, DGRP and DVOL hold one u32 (a running number) and
//   MCTL holds two zero u32; CNKH, kns2 and knsC hold their children
//   directly. walk_chunks skips these words and reports the container's data
//   whole, words included.
//   A CM1R is consumed by the next DIP inside the same innermost DVOL or
//   DGRP (7,768 of 7,768); 11 DIPs sit directly under a DGRP; 46,015 DIPs
//   have no bone map and take kNoBoneMap.
//   A TS2V entry's name offset is from the TS2V record's start (PW0's first
//   name at +0x150 = the table at +0x50 plus 16 entries of 16 bytes).
//   Buffer record names carry the buffer number as three decimal digits
//   ("vertex_000.bin"; 4,326 of 4,326 buffer and shape records).
//
// Readings of the header that the checks encode:
//   "Current material" for a draw is the most recent material in document
//   order, mesh-wide; a draw before any material is kNoMaterial. No shipped
//   draw precedes its group's first material, so the data does not choose
//   between this and a per-group reset.
//   A buffer chunk (VTX, VTXS, IDX) that appears before any VDCL, or a
//   second one of a kind in one group, or one too short to name its buffer
//   (VTX and VTXS under 16 bytes, IDX under 4), or a name with no record of
//   the right tag, is BufferMissing at that chunk. A short VTX or VTXS has
//   no stride, so the layout check passes over its group and leaves the
//   failure to BufferMissing rather than calling a stride of 0 LayoutInvalid.
//   A VTXS makes the group's layout two-stream; a declaration that uses
//   stream 1 without a VTXS is BufferMissing, a VTXS with no stream-1
//   element is LayoutInvalid (the validator's StreamUnused). The stream
//   word of VTX and VTXS is not checked: the chunk's tag names the stream.
//   More than kVertexAttributeCount elements is LayoutInvalid (some slot
//   repeats); an element on a stream other than 0 or 1, a method other than
//   0, or a (type, usage, index) outside the header's table is
//   DeclarationUnsupported, as is a VDCL without its terminator.
//   A stream-0 buffer with no vertices, or a stream-1 buffer that does not
//   hold exactly the group's vertex count, is BufferSizeNotMultipleOfStride.
//   An index buffer with an odd byte count, no indices, or a count that is
//   not a multiple of 3 is IndexBufferSizeInvalid.
//   A DIP whose data is not six u32, or whose primitive count is 0, is
//   DrawRangeInvalid.
//   A CM1R outside any DVOL or DGRP, a CM1R no DIP of its container
//   consumes, or a second CM1R before the first is consumed, is
//   BoneMapMalformed, as is a map whose size is not 8 + 16 * count, whose
//   entry tag is not "TNIJ" or whose slot is not its position.
//   A material without any knsS before it, a knsS shorter than two words, a
//   CF1I, TR, SSSM or SRS whose size or count does not match its stated
//   layout, one that is not a direct child of an MCTL inside a kns2 or
//   knsC, or a second one of a kind in one material, or a TR, SSSM or SRS
//   in a container with no CF1I, is MaterialBlockMalformed. The CF1I tags
//   are kept as stored; their order is material_mapping's to read.
//   A TS2V shorter than its header, without its magic and byte-order word,
//   of a version other than 1 or 2, with no shapes, with an entry table or a
//   name outside the record, with an empty or unterminated name, whose
//   size is not its data offset plus shapes * vertices * 8, or whose vertex
//   count differs from its group's, is MorphSetMalformed.
//   A knsS longer than two words is read for its two words and its tag is
//   listed in unread_chunks, as are GENV, TANM, TRST and any tag the reader
//   does not know. unread_chunks holds header chunk tags only: the walk
//   covers header.bin alone, so the container's other USER records
//   (anim_mat.bin, volumeclip.bin) and every record's slot bytes are never
//   visited and never listed.
namespace bw::content_converter {

namespace {

using Tag = std::array<char, 4>;

constexpr Tag kCnkh{'C', 'N', 'K', 'H'};
constexpr Tag kDgrp{'D', 'G', 'R', 'P'};
constexpr Tag kDvol{'D', 'V', 'O', 'L'};
constexpr Tag kKns2{'k', 'n', 's', '2'};
constexpr Tag kKnsC{'k', 'n', 's', 'C'};
constexpr Tag kMctl{'M', 'C', 'T', 'L'};
constexpr Tag kVdcl{'V', 'D', 'C', 'L'};
constexpr Tag kVtx{'V', 'T', 'X', ' '};
constexpr Tag kVtxs{'V', 'T', 'X', 'S'};
constexpr Tag kIdx{'I', 'D', 'X', ' '};
constexpr Tag kKnsS{'k', 'n', 's', 'S'};
constexpr Tag kCf1i{'C', 'F', '1', 'I'};
constexpr Tag kTr{'T', 'R', ' ', ' '};
constexpr Tag kSssm{'S', 'S', 'S', 'M'};
constexpr Tag kSrs{'S', 'R', 'S', ' '};
constexpr Tag kLenv{'L', 'E', 'N', 'V'};
constexpr Tag kCm1r{'C', 'M', '1', 'R'};
constexpr Tag kDip{'D', 'I', 'P', ' '};
// The CM1R entry tag as the file stores it.
constexpr Tag kJointStored{'T', 'N', 'I', 'J'};
constexpr Tag kTs2v{'T', 'S', '2', 'V'};

constexpr std::uint32_t kByteOrderWord = 0x11223344U;
constexpr std::size_t kBoundsAt = 0x10;
constexpr std::size_t kChunkCountAt = 0x58;
constexpr std::size_t kChunkWordsAt = 0x5C;
constexpr std::size_t kChunkHeaderBytes = 8;
constexpr std::uint32_t kTriangleList = 4;
constexpr std::size_t kDipBytes = 24;
// VTX and VTXS: stream, buffer number, 0, stride. IDX: buffer number.
constexpr std::size_t kVertexBufferChunkBytes = 16;
constexpr std::size_t kIndexBufferChunkBytes = 4;
constexpr std::size_t kElementBytes = 8;
constexpr std::uint32_t kEndStream = 0xFF;
constexpr std::uint32_t kEndType = 17;
constexpr std::size_t kCf1iRecords = 9;
constexpr std::size_t kCf1iRecordBytes = 32;
constexpr std::size_t kListEntryBytes = 16;
constexpr std::size_t kTs2vHeaderBytes = 0x50;
constexpr std::size_t kTs2vNameBytes = 32;

struct ContainerRule {
  Tag tag{};
  std::uint32_t prefix{};
};
constexpr std::array<ContainerRule, 6> kContainers{{
    {kCnkh, 0},
    {kDgrp, 4},
    {kDvol, 4},
    {kKns2, 0},
    {kKnsC, 0},
    {kMctl, 8},
}};

std::optional<std::uint32_t> container_prefix(Tag tag) noexcept {
  for (const ContainerRule& rule : kContainers) {
    if (rule.tag == tag) {
      return rule.prefix;
    }
  }
  return std::nullopt;
}

MeshReadFailure fail(MeshReadError error, std::uint32_t offset, Tag tag = {}) noexcept {
  return {error, offset, tag};
}
MeshReadFailure fail_at(MeshReadError error, const Chunk& chunk) noexcept {
  return {error, chunk.offset, chunk.tag};
}

// One declaration element mapped to its attribute slot and element type.
struct ElementMapping {
  std::uint32_t usage{};
  std::uint32_t usage_index{};
  geometry::VertexAttribute attribute{};
};
constexpr std::array<ElementMapping, 12> kUsages{{
    {0, 0, geometry::VertexAttribute::Position},
    {1, 0, geometry::VertexAttribute::BoneSlots},
    {1, 1, geometry::VertexAttribute::BoneWeights},
    {3, 0, geometry::VertexAttribute::Normal},
    {5, 1, geometry::VertexAttribute::TexCoord1},
    {5, 2, geometry::VertexAttribute::TexCoord2},
    {5, 3, geometry::VertexAttribute::TexCoord3},
    {5, 4, geometry::VertexAttribute::TexCoord4},
    {6, 0, geometry::VertexAttribute::Tangent},
    {10, 0, geometry::VertexAttribute::Color0},
    {10, 1, geometry::VertexAttribute::Color1},
    {10, 2, geometry::VertexAttribute::Color2},
}};

std::optional<geometry::VertexElementType> element_type(std::uint32_t type) noexcept {
  switch (type) {
  case 2:
    return geometry::VertexElementType::Float3;
  case 5:
    return geometry::VertexElementType::UByte4;
  case 8:
    return geometry::VertexElementType::UByte4Norm;
  case 10:
    return geometry::VertexElementType::Short4Norm;
  case 15:
    return geometry::VertexElementType::Half2;
  default:
    return std::nullopt;
  }
}

std::optional<geometry::VertexAttribute> element_attribute(std::uint32_t usage,
                                                           std::uint32_t index) noexcept {
  for (const ElementMapping& mapping : kUsages) {
    if (mapping.usage == usage && mapping.usage_index == index) {
      return mapping.attribute;
    }
  }
  return std::nullopt;
}

struct Ancestor {
  Tag tag{};
  std::size_t chunk{};
  std::uint64_t end{};
};

// A buffer chunk of a group, when the group has one.
struct BufferSlot {
  std::size_t chunk{};
  bool present{};
};

struct GroupSource {
  std::size_t vdcl{};
  BufferSlot vtx{};
  BufferSlot vtxs{};
  BufferSlot idx{};
  bool buffer_misplaced{};
  std::size_t misplaced_chunk{};
  // The declaration uses stream 1 or the group has a VTXS.
  bool two_streams{};
  std::vector<geometry::VertexAttributeDescription> attributes{};
  geometry::VertexStreamLayout layout{};
  std::uint32_t vertex_count{};
  std::uint32_t index_count{};
  const CprRecord* stream0{};
  const CprRecord* stream1{};
  const CprRecord* indices{};
};

struct DrawSource {
  std::size_t dip{};
  std::size_t group{};
  bool in_group{};
  std::uint32_t material{geometry::kNoMaterial};
  std::uint32_t bone_map{geometry::kNoBoneMap};
};

struct MaterialSource {
  std::size_t container{};
  std::optional<std::size_t> knss{};
  std::optional<std::size_t> cf1i{};
  std::optional<std::size_t> tr{};
  std::optional<std::size_t> sssm{};
  std::optional<std::size_t> srs{};
  bool duplicate{};
  std::size_t duplicate_chunk{};
};

struct BoneMapSource {
  std::size_t cm1r{};
  bool consumed{};
  bool misplaced{};
};

// The document-order pairing: what each chunk belongs to, with nothing
// judged yet except what can only be seen in passing.
struct Pairing {
  std::vector<GroupSource> groups{};
  std::vector<DrawSource> draws{};
  // Every kns2 or knsC, material or not, in document order.
  std::vector<MaterialSource> candidates{};
  std::vector<BoneMapSource> maps{};
  // A material parameter chunk outside an MCTL of a kns2 or knsC.
  std::optional<std::size_t> stray_parameter{};
  std::vector<Tag> unread{};
};

void note_unread(Pairing& p, Tag tag) {
  if (std::find(p.unread.begin(), p.unread.end(), tag) == p.unread.end()) {
    p.unread.push_back(tag);
  }
}

std::uint64_t chunk_end(const Chunk& c) noexcept {
  return static_cast<std::uint64_t>(c.offset) + kChunkHeaderBytes + c.data.size();
}

// Pairs every chunk in document order. Material indices and draw materials
// are assigned when each candidate container closes, which is before any
// later chunk: no DIP lives inside a kns2 or knsC.
Pairing pair_chunks(const std::vector<Chunk>& chunks) {
  Pairing p;
  std::vector<Ancestor> stack;
  std::optional<std::size_t> last_knss;
  std::uint32_t materials = 0;
  std::uint32_t current_material = geometry::kNoMaterial;
  struct Pending {
    std::size_t map{};
    std::size_t scope_chunk{};
  };
  std::optional<Pending> pending;

  const auto close = [&](const Ancestor& a) {
    if (a.tag == kKns2 || a.tag == kKnsC) {
      for (std::size_t c = 0; c < p.candidates.size(); ++c) {
        if (p.candidates[c].container == a.chunk && p.candidates[c].cf1i.has_value()) {
          current_material = materials++;
        }
      }
    }
    if (pending.has_value() && pending->scope_chunk == a.chunk) {
      p.maps[pending->map].misplaced = true;
      pending.reset();
    }
  };
  const auto innermost = [&](auto predicate) -> std::optional<std::size_t> {
    for (std::size_t i = stack.size(); i > 0; --i) {
      if (predicate(stack[i - 1].tag)) {
        return i - 1;
      }
    }
    return std::nullopt;
  };

  for (std::size_t i = 0; i < chunks.size(); ++i) {
    const Chunk& c = chunks[i];
    while (!stack.empty() && stack.back().end <= c.offset) {
      close(stack.back());
      stack.pop_back();
    }
    const Tag t = c.tag;
    GroupSource* group = p.groups.empty() ? nullptr : &p.groups.back();
    if (t == kVdcl) {
      p.groups.push_back(GroupSource{i});
    } else if (t == kVtx || t == kVtxs || t == kIdx) {
      BufferSlot* slot = nullptr;
      if (group != nullptr) {
        slot = t == kVtx ? &group->vtx : (t == kVtxs ? &group->vtxs : &group->idx);
      }
      if (slot == nullptr || slot->present) {
        if (group == nullptr) {
          p.groups.push_back(GroupSource{i});
          group = &p.groups.back();
        }
        if (!group->buffer_misplaced) {
          group->buffer_misplaced = true;
          group->misplaced_chunk = i;
        }
      } else {
        *slot = BufferSlot{i, true};
      }
    } else if (t == kKnsS) {
      last_knss = i;
      if (c.data.size() > 8U) {
        note_unread(p, t);
      }
    } else if (t == kKns2 || t == kKnsC) {
      p.candidates.push_back(MaterialSource{i, last_knss});
    } else if (t == kCf1i || t == kTr || t == kSssm || t == kSrs) {
      // Direct child of an MCTL whose parent is the candidate container.
      const bool placed =
          stack.size() >= 2 && stack.back().tag == kMctl &&
          (stack[stack.size() - 2].tag == kKns2 || stack[stack.size() - 2].tag == kKnsC);
      if (!placed) {
        if (!p.stray_parameter.has_value()) {
          p.stray_parameter = i;
        }
      } else {
        const std::size_t container = stack[stack.size() - 2].chunk;
        for (MaterialSource& m : p.candidates) {
          if (m.container != container) {
            continue;
          }
          std::optional<std::size_t>& slot =
              t == kCf1i ? m.cf1i : (t == kTr ? m.tr : (t == kSssm ? m.sssm : m.srs));
          if (slot.has_value()) {
            if (!m.duplicate) {
              m.duplicate = true;
              m.duplicate_chunk = i;
            }
          } else {
            slot = i;
          }
        }
      }
    } else if (t == kCm1r) {
      const std::size_t map = p.maps.size();
      p.maps.push_back(BoneMapSource{i});
      const std::optional<std::size_t> scope =
          innermost([](const Tag& a) { return a == kDvol || a == kDgrp; });
      if (!scope.has_value()) {
        p.maps[map].misplaced = true;
      } else {
        if (pending.has_value()) {
          p.maps[pending->map].misplaced = true;
        }
        pending = Pending{map, stack[*scope].chunk};
      }
    } else if (t == kDip) {
      DrawSource draw{i};
      if (!p.groups.empty()) {
        draw.group = p.groups.size() - 1U;
        draw.in_group = true;
      }
      draw.material = current_material;
      if (pending.has_value()) {
        const std::optional<std::size_t> scope =
            innermost([](const Tag& a) { return a == kDvol || a == kDgrp; });
        if (scope.has_value() && stack[*scope].chunk == pending->scope_chunk) {
          draw.bone_map = static_cast<std::uint32_t>(pending->map);
          p.maps[pending->map].consumed = true;
        } else {
          p.maps[pending->map].misplaced = true;
        }
        pending.reset();
      }
      p.draws.push_back(draw);
    } else if (!is_container_tag(t)) {
      // A header chunk tag; records outside header.bin are not walked.
      note_unread(p, t);
    }
    if (container_prefix(t).has_value()) {
      stack.push_back(Ancestor{t, i, chunk_end(c)});
    }
  }
  while (!stack.empty()) {
    close(stack.back());
    stack.pop_back();
  }
  if (pending.has_value()) {
    p.maps[pending->map].misplaced = true;
  }
  return p;
}

std::string buffer_name(std::string_view stem, std::uint32_t number) {
  std::array<char, 3> digits{};
  std::uint32_t n = number;
  for (std::size_t i = digits.size(); i > 0; --i) {
    digits[i - 1] = static_cast<char>('0' + (n % 10U));
    n /= 10U;
  }
  std::string name(stem);
  name += '_';
  // A number over 999 keeps all its digits; the content's names have three.
  if (number > 999U) {
    name += std::to_string(number);
  } else {
    name.append(digits.data(), digits.size());
  }
  name += ".bin";
  return name;
}

const CprRecord* find_tagged(const CprContainer& container, std::string_view name,
                             RecordTag tag) noexcept {
  const CprRecord* record = container.find_record(name);
  return record != nullptr && record->tag == tag ? record : nullptr;
}

std::vector<std::byte> copy_bytes(std::span<const std::byte> bytes) {
  return {bytes.begin(), bytes.end()};
}

// The two-word knsS, the nine-record CF1I and the TR, SSSM and SRS lists,
// each checked against its stated layout.
bool read_material(const std::vector<Chunk>& chunks, const MaterialSource& m, MaterialBlock& out) {
  if (!m.knss.has_value() || !m.cf1i.has_value()) {
    return false;
  }
  const std::span<const std::byte> knss = chunks[*m.knss].data;
  if (knss.size() < 8U) {
    return false;
  }
  out.knss_feature = bytes::u32(knss, 0);
  out.knss_second = bytes::u32(knss, 4);
  const std::span<const std::byte> cf1i = chunks[*m.cf1i].data;
  if (cf1i.size() != 8U + (kCf1iRecords * kCf1iRecordBytes) ||
      bytes::u32(cf1i, 4) != kCf1iRecords) {
    return false;
  }
  for (std::size_t r = 0; r < kCf1iRecords; ++r) {
    const std::size_t at = 8U + (r * kCf1iRecordBytes);
    out.cf1i[r].tag = bytes::reversed_tag(cf1i, at);
    for (std::size_t k = 0; k < 4; ++k) {
      out.cf1i[r].value[k] = bytes::f32(cf1i, at + 16U + (4U * k));
    }
  }
  if (m.tr.has_value()) {
    const std::span<const std::byte> tr = chunks[*m.tr].data;
    if (tr.size() < 8U) {
      return false;
    }
    const std::uint64_t count = bytes::u32(tr, 4);
    if (tr.size() != 8U + (count * kListEntryBytes)) {
      return false;
    }
    for (std::size_t s = 0; s < count; ++s) {
      const std::size_t at = 8U + (s * kListEntryBytes);
      out.slots.push_back(TrSlot{bytes::reversed_tag(tr, at), bytes::u32(tr, at + 8)});
    }
  }
  const auto one_entry = [](std::span<const std::byte> data) {
    return data.size() == 8U + kListEntryBytes && bytes::u32(data, 4) == 1U;
  };
  if (m.sssm.has_value()) {
    const std::span<const std::byte> sssm = chunks[*m.sssm].data;
    if (!one_entry(sssm) || bytes::reversed_tag(sssm, 8) != kLenv) {
      return false;
    }
    out.has_sssm = true;
    out.sssm_lenv = {bytes::u32(sssm, 12), bytes::u32(sssm, 16), bytes::u32(sssm, 20)};
  }
  if (m.srs.has_value()) {
    const std::span<const std::byte> srs = chunks[*m.srs].data;
    if (!one_entry(srs)) {
      return false;
    }
    out.has_srs = true;
    out.srs_tag = bytes::reversed_tag(srs, 8);
    out.srs_value = {bytes::u32(srs, 12), bytes::u32(srs, 16), bytes::u32(srs, 20)};
  }
  return true;
}

bool read_bone_map(std::span<const std::byte> data, std::vector<std::uint32_t>& out) {
  if (data.size() < 8U) {
    return false;
  }
  const std::uint64_t count = bytes::u32(data, 4);
  if (data.size() != 8U + (count * kListEntryBytes)) {
    return false;
  }
  for (std::size_t e = 0; e < count; ++e) {
    const std::size_t at = 8U + (e * kListEntryBytes);
    if (bytes::tag(data, at) != kJointStored || bytes::u32(data, at + 12) != e) {
      return false;
    }
    out.push_back(bytes::u32(data, at + 8));
  }
  return true;
}

// The set's targets and vertex count; the caller names the group and
// compares the count with the group's.
bool read_morph_set(std::span<const std::byte> data, MorphTargetsData& out) {
  if (data.size() < kTs2vHeaderBytes || bytes::tag(data, 0) != kTs2v ||
      bytes::u32(data, 4) != kByteOrderWord) {
    return false;
  }
  const std::uint32_t version = bytes::u32(data, 8);
  const std::uint64_t vertices = bytes::u32(data, 0x0C);
  const std::uint64_t shapes = bytes::u32(data, 0x10);
  const std::uint64_t table = bytes::u32(data, 0x48);
  const std::uint64_t deltas = bytes::u32(data, 0x4C);
  const std::uint64_t target_bytes = vertices * geometry::kMorphDeltaBytes;
  if ((version != 1U && version != 2U) || shapes == 0U || vertices == 0U ||
      !bytes::within(table, shapes * kListEntryBytes, data.size()) || deltas > data.size() ||
      (data.size() - deltas) % target_bytes != 0U ||
      (data.size() - deltas) / target_bytes != shapes) {
    return false;
  }
  out.vertex_count = static_cast<std::uint32_t>(vertices);
  for (std::size_t s = 0; s < shapes; ++s) {
    const std::uint64_t name_at = bytes::u32(data, table + (s * kListEntryBytes));
    if (!bytes::within(name_at, kTs2vNameBytes, data.size())) {
      return false;
    }
    const bytes::CString name = bytes::c_string(data.subspan(name_at, kTs2vNameBytes), 0);
    if (!name.found || name.text.empty()) {
      return false;
    }
    out.targets.push_back(MorphTargetData{
        std::string(name.text),
        copy_bytes(data.subspan(static_cast<std::size_t>(deltas + (s * target_bytes)),
                                static_cast<std::size_t>(target_bytes)))});
  }
  return true;
}

// "shapeoffset_NNN.bin" with three decimal digits.
bool is_shape_offset_name(std::string_view name) noexcept {
  constexpr std::string_view kStem = "shapeoffset_";
  constexpr std::string_view kExtension = ".bin";
  if (name.size() != kStem.size() + 3U + kExtension.size() || !name.starts_with(kStem) ||
      !name.ends_with(kExtension)) {
    return false;
  }
  return std::all_of(name.begin() + static_cast<std::ptrdiff_t>(kStem.size()),
                     name.begin() + static_cast<std::ptrdiff_t>(kStem.size() + 3U),
                     [](char c) { return c >= '0' && c <= '9'; });
}

} // namespace

bool is_container_tag(std::array<char, 4> tag) noexcept {
  return container_prefix(tag).has_value();
}

MeshReadFailure walk_chunks(std::span<const std::byte> header_payload, std::vector<Chunk>& out) {
  using E = MeshReadError;
  out.clear();
  const std::span<const std::byte> h = header_payload;
  if (h.size() < 8U || bytes::tag(h, 0) != kModelMagic || bytes::u32(h, 4) != kByteOrderWord) {
    return fail(E::BadModelMagic, 0);
  }
  if (h.size() < kChunkWordsAt) {
    return fail(E::ChunkOverrun, 0);
  }
  const std::uint64_t count = bytes::u32(h, kChunkCountAt);
  std::uint64_t pos = kChunkWordsAt + (4U * count);
  if (pos > h.size()) {
    return fail(E::ChunkOverrun, static_cast<std::uint32_t>(kChunkCountAt));
  }
  struct Frame {
    std::uint64_t cursor{};
    std::uint64_t end{};
    std::uint32_t depth{};
  };
  std::vector<Frame> frames;
  for (std::uint64_t k = 0; k < count; ++k) {
    // u32 index, then the CNKH chunk.
    if (!bytes::within(pos, 4U + kChunkHeaderBytes, h.size())) {
      return fail(E::ChunkOverrun, static_cast<std::uint32_t>(pos));
    }
    const auto tag_at = static_cast<std::size_t>(pos + 4U);
    const Tag tag = bytes::reversed_tag(h, tag_at);
    const std::uint64_t size = bytes::u32(h, tag_at + 4U);
    const std::uint64_t data_at = tag_at + kChunkHeaderBytes;
    if (tag != kCnkh || !bytes::within(data_at, size, h.size())) {
      return fail(E::ChunkOverrun, static_cast<std::uint32_t>(tag_at), tag);
    }
    out.push_back(
        Chunk{tag, static_cast<std::uint32_t>(tag_at),
              h.subspan(static_cast<std::size_t>(data_at), static_cast<std::size_t>(size)), 0});
    frames.push_back(Frame{data_at, data_at + size, 1});
    while (!frames.empty()) {
      Frame& f = frames.back();
      if (f.cursor == f.end) {
        frames.pop_back();
        continue;
      }
      const std::uint64_t at = f.cursor;
      if (f.end - at < kChunkHeaderBytes) {
        return fail(E::ChunkOverrun, static_cast<std::uint32_t>(at));
      }
      const Tag child = bytes::reversed_tag(h, static_cast<std::size_t>(at));
      const std::uint64_t child_size = bytes::u32(h, static_cast<std::size_t>(at) + 4U);
      const std::uint64_t child_data = at + kChunkHeaderBytes;
      if (child_size > f.end - child_data) {
        return fail(E::ChunkOverrun, static_cast<std::uint32_t>(at), child);
      }
      const std::uint32_t depth = f.depth;
      f.cursor = child_data + child_size;
      out.push_back(Chunk{
          child, static_cast<std::uint32_t>(at),
          h.subspan(static_cast<std::size_t>(child_data), static_cast<std::size_t>(child_size)),
          depth});
      if (const std::optional<std::uint32_t> prefix = container_prefix(child)) {
        if (*prefix > child_size) {
          return fail(E::ChunkOverrun, static_cast<std::uint32_t>(at), child);
        }
        frames.push_back(Frame{child_data + *prefix, child_data + child_size, depth + 1U});
      }
    }
    pos = data_at + size;
  }
  if (pos != h.size()) {
    return fail(E::ChunkOverrun, static_cast<std::uint32_t>(pos));
  }
  return {};
}

MeshReadFailure read_mesh_container(const CprContainer& container, MeshSource& out) {
  using E = MeshReadError;
  out = MeshSource{};
  const CprRecord* header = find_tagged(container, "header.bin", RecordTag::User);
  if (header == nullptr) {
    return fail(E::HeaderRecordMissing, 0);
  }
  std::vector<Chunk> chunks;
  if (const MeshReadFailure walked = walk_chunks(header->payload, chunks); !walked.ok()) {
    return walked;
  }
  Pairing p = pair_chunks(chunks);

  // DeclarationUnsupported: every VDCL's elements, mapped.
  for (GroupSource& g : p.groups) {
    const Chunk& vdcl = chunks[g.vdcl];
    if (vdcl.tag != kVdcl) {
      continue; // A buffer chunk before any VDCL; BufferMissing below.
    }
    const std::span<const std::byte> d = vdcl.data;
    bool ended = false;
    for (std::size_t at = 4; at + kElementBytes <= d.size(); at += kElementBytes) {
      const std::uint32_t stream = bytes::u16(d, at);
      const std::uint32_t type = bytes::u8(d, at + 4);
      if (stream == kEndStream && type == kEndType) {
        ended = true;
        break;
      }
      const std::optional<geometry::VertexElementType> element = element_type(type);
      const std::optional<geometry::VertexAttribute> attribute =
          element_attribute(bytes::u8(d, at + 6), bytes::u8(d, at + 7));
      if (stream > 1U || bytes::u8(d, at + 5) != 0U || !element.has_value() ||
          !attribute.has_value()) {
        return fail_at(E::DeclarationUnsupported, vdcl);
      }
      g.attributes.push_back(geometry::VertexAttributeDescription{
          *attribute, *element, static_cast<std::uint8_t>(stream), bytes::u16(d, at + 2)});
    }
    if (!ended) {
      return fail_at(E::DeclarationUnsupported, vdcl);
    }
  }

  // LayoutInvalid: the declaration with the strides its buffer chunks give.
  // A vertex buffer chunk names its stride in its fourth word.
  const auto names_stride = [&](const BufferSlot& slot) {
    return slot.present && chunks[slot.chunk].data.size() >= kVertexBufferChunkBytes;
  };
  const auto stride_of = [&](const BufferSlot& slot) -> std::uint32_t {
    return bytes::u32(chunks[slot.chunk].data, 12);
  };
  for (GroupSource& g : p.groups) {
    if (chunks[g.vdcl].tag != kVdcl) {
      continue;
    }
    const bool uses_stream1 =
        std::any_of(g.attributes.begin(), g.attributes.end(),
                    [](const geometry::VertexAttributeDescription& a) { return a.stream == 1U; });
    const bool needs_stream1 = uses_stream1 || g.vtxs.present;
    g.two_streams = needs_stream1;
    if (!names_stride(g.vtx) || (needs_stream1 && !names_stride(g.vtxs))) {
      continue; // BufferMissing below: absent, or too short to name a stride.
    }
    if (g.attributes.size() > geometry::kVertexAttributeCount) {
      return fail_at(E::LayoutInvalid, chunks[g.vdcl]);
    }
    geometry::VertexStreamLayout& layout = g.layout;
    std::copy(g.attributes.begin(), g.attributes.end(), layout.attributes.begin());
    layout.attribute_count = static_cast<std::uint8_t>(g.attributes.size());
    layout.stream_count = needs_stream1 ? 2U : 1U;
    layout.streams[0].stride = stride_of(g.vtx);
    layout.streams[1].stride = needs_stream1 ? stride_of(g.vtxs) : 0U;
    if (geometry::validate_vertex_stream_layout(layout) != geometry::VertexStreamError::None) {
      return fail_at(E::LayoutInvalid, chunks[g.vdcl]);
    }
  }

  // BufferMissing: every group's buffer chunks and the records they name.
  // Every group the layout pass passed over is named here, by its absent or
  // short buffer chunk, so each group past this pass has its layout.
  for (GroupSource& g : p.groups) {
    if (g.buffer_misplaced) {
      return fail_at(E::BufferMissing, chunks[g.misplaced_chunk]);
    }
    const Chunk& vdcl = chunks[g.vdcl];
    // An absent buffer chunk has no offset of its own; the group's VDCL is
    // named instead.
    if (!g.vtx.present || !g.idx.present || (g.two_streams && !g.vtxs.present)) {
      return fail_at(E::BufferMissing, vdcl);
    }
    const Chunk& vtx = chunks[g.vtx.chunk];
    const Chunk& idx = chunks[g.idx.chunk];
    if (vtx.data.size() < kVertexBufferChunkBytes) {
      return fail_at(E::BufferMissing, vtx);
    }
    if (idx.data.size() < kIndexBufferChunkBytes) {
      return fail_at(E::BufferMissing, idx);
    }
    g.stream0 =
        find_tagged(container, buffer_name("vertex", bytes::u32(vtx.data, 4)), RecordTag::Dxvb);
    g.indices =
        find_tagged(container, buffer_name("index", bytes::u32(idx.data, 0)), RecordTag::Dxib);
    if (g.stream0 == nullptr) {
      return fail_at(E::BufferMissing, vtx);
    }
    if (g.indices == nullptr) {
      return fail_at(E::BufferMissing, idx);
    }
    if (g.two_streams) {
      const Chunk& vtxs = chunks[g.vtxs.chunk];
      if (vtxs.data.size() < kVertexBufferChunkBytes) {
        return fail_at(E::BufferMissing, vtxs);
      }
      g.stream1 = find_tagged(container, buffer_name("shapevertex", bytes::u32(vtxs.data, 4)),
                              RecordTag::Dxvb);
      if (g.stream1 == nullptr) {
        return fail_at(E::BufferMissing, vtxs);
      }
    }
  }

  // BufferSizeNotMultipleOfStride.
  for (GroupSource& g : p.groups) {
    const std::uint64_t stride0 = g.layout.streams[0].stride;
    const std::uint64_t bytes0 = g.stream0->payload.size();
    if (bytes0 == 0U || bytes0 % stride0 != 0U || bytes0 / stride0 > UINT32_MAX) {
      return fail_at(E::BufferSizeNotMultipleOfStride, chunks[g.vtx.chunk]);
    }
    g.vertex_count = static_cast<std::uint32_t>(bytes0 / stride0);
    if (g.layout.stream_count == 2U &&
        g.stream1->payload.size() !=
            static_cast<std::uint64_t>(g.vertex_count) * g.layout.streams[1].stride) {
      return fail_at(E::BufferSizeNotMultipleOfStride, chunks[g.vtxs.chunk]);
    }
  }

  // IndexBufferSizeInvalid.
  for (GroupSource& g : p.groups) {
    const std::uint64_t index_bytes = g.indices->payload.size();
    const std::uint64_t count = index_bytes / 2U;
    if (index_bytes % 2U != 0U || count == 0U || count % 3U != 0U || count > UINT32_MAX) {
      return fail_at(E::IndexBufferSizeInvalid, chunks[g.idx.chunk]);
    }
    g.index_count = static_cast<std::uint32_t>(count);
  }

  // DrawOutsideGroup.
  for (const DrawSource& d : p.draws) {
    if (!d.in_group) {
      return fail_at(E::DrawOutsideGroup, chunks[d.dip]);
    }
  }

  // DrawRangeInvalid, then PrimitiveTypeUnsupported.
  for (const DrawSource& d : p.draws) {
    const Chunk& dip = chunks[d.dip];
    if (dip.data.size() != kDipBytes) {
      return fail_at(E::DrawRangeInvalid, dip);
    }
    const GroupSource& g = p.groups[d.group];
    const std::uint64_t base = bytes::u32(dip.data, 4);
    const std::uint64_t min_index = bytes::u32(dip.data, 8);
    const std::uint64_t vertices = bytes::u32(dip.data, 12);
    const std::uint64_t first = bytes::u32(dip.data, 16);
    const std::uint64_t primitives = bytes::u32(dip.data, 20);
    if (primitives == 0U || first + (3U * primitives) > g.index_count ||
        base + min_index + vertices > g.vertex_count) {
      return fail_at(E::DrawRangeInvalid, dip);
    }
  }
  for (const DrawSource& d : p.draws) {
    if (bytes::u32(chunks[d.dip].data, 0) != kTriangleList) {
      return fail_at(E::PrimitiveTypeUnsupported, chunks[d.dip]);
    }
  }

  // BoneMapMalformed.
  std::vector<std::vector<std::uint32_t>> bone_maps(p.maps.size());
  for (std::size_t m = 0; m < p.maps.size(); ++m) {
    const Chunk& cm1r = chunks[p.maps[m].cm1r];
    if (p.maps[m].misplaced || !p.maps[m].consumed || !read_bone_map(cm1r.data, bone_maps[m])) {
      return fail_at(E::BoneMapMalformed, cm1r);
    }
  }

  // MaterialBlockMalformed.
  if (p.stray_parameter.has_value()) {
    return fail_at(E::MaterialBlockMalformed, chunks[*p.stray_parameter]);
  }
  std::vector<MaterialBlock> materials;
  for (const MaterialSource& m : p.candidates) {
    if (m.duplicate) {
      return fail_at(E::MaterialBlockMalformed, chunks[m.duplicate_chunk]);
    }
    if (!m.cf1i.has_value()) {
      if (m.tr.has_value() || m.sssm.has_value() || m.srs.has_value()) {
        return fail_at(E::MaterialBlockMalformed, chunks[m.container]);
      }
      continue; // A bone-map container, not a material.
    }
    MaterialBlock block;
    if (!read_material(chunks, m, block)) {
      return fail_at(E::MaterialBlockMalformed, chunks[m.container]);
    }
    materials.push_back(std::move(block));
  }

  // MorphSetMalformed: each group with a VTXS takes shapeoffset_NNN if the
  // container holds it, numbered in group order.
  std::vector<std::uint32_t> morph_set_of_group(p.groups.size(), geometry::kNoMorphSet);
  std::vector<const CprRecord*> used_shape_records;
  for (std::size_t gi = 0; gi < p.groups.size(); ++gi) {
    const GroupSource& g = p.groups[gi];
    if (!g.vtxs.present) {
      continue;
    }
    const CprRecord* record =
        find_tagged(container, buffer_name("shapeoffset", bytes::u32(chunks[g.vtxs.chunk].data, 4)),
                    RecordTag::User);
    if (record == nullptr) {
      continue;
    }
    MorphTargetsData set;
    set.group = static_cast<std::uint32_t>(gi);
    if (!read_morph_set(record->payload, set) || set.vertex_count != g.vertex_count) {
      return fail_at(E::MorphSetMalformed, chunks[g.vtxs.chunk]);
    }
    morph_set_of_group[gi] = static_cast<std::uint32_t>(out.morph_sets.size());
    out.morph_sets.push_back(std::move(set));
    used_shape_records.push_back(record);
  }

  // MorphSetGroupMissing.
  for (const CprRecord& record : container.records()) {
    if (record.tag == RecordTag::User && is_shape_offset_name(record.short_name) &&
        std::find(used_shape_records.begin(), used_shape_records.end(), &record) ==
            used_shape_records.end()) {
      return fail(E::MorphSetGroupMissing, 0);
    }
  }

  // Fill.
  MeshAssetData& mesh = out.mesh;
  const std::span<const std::byte> hp = header->payload;
  for (std::size_t k = 0; k < 3; ++k) {
    mesh.bounds_max[k] = bytes::f32(hp, kBoundsAt + (4U * k));
    mesh.bounds_min[k] = bytes::f32(hp, kBoundsAt + 0x10U + (4U * k));
    mesh.center[k] = bytes::f32(hp, kBoundsAt + 0x20U + (4U * k));
  }
  mesh.radius = bytes::f32(hp, kBoundsAt + 0x30U);
  mesh.material_count = static_cast<std::uint32_t>(materials.size());
  mesh.bone_maps = std::move(bone_maps);
  for (std::size_t gi = 0; gi < p.groups.size(); ++gi) {
    const GroupSource& g = p.groups[gi];
    MeshGroupData group;
    group.layout = g.layout;
    group.vertex_count = g.vertex_count;
    group.stream0 = copy_bytes(g.stream0->payload);
    if (g.stream1 != nullptr) {
      group.stream1 = copy_bytes(g.stream1->payload);
    }
    group.indices.resize(g.index_count);
    for (std::size_t i = 0; i < g.index_count; ++i) {
      group.indices[i] = static_cast<std::uint16_t>(bytes::u16(g.indices->payload, 2U * i));
    }
    group.first_draw = static_cast<std::uint32_t>(mesh.draws.size());
    for (const DrawSource& d : p.draws) {
      if (d.group != gi) {
        continue;
      }
      const std::span<const std::byte> dip = chunks[d.dip].data;
      geometry::MeshDrawRecord draw{};
      draw.base_vertex = bytes::u32(dip, 4);
      draw.min_index = bytes::u32(dip, 8);
      draw.vertex_count = bytes::u32(dip, 12);
      draw.first_index = bytes::u32(dip, 16);
      draw.index_count = 3U * bytes::u32(dip, 20);
      draw.material = d.material;
      draw.bone_map = d.bone_map;
      mesh.draws.push_back(draw);
    }
    group.draw_count = static_cast<std::uint32_t>(mesh.draws.size()) - group.first_draw;
    group.morph_set = morph_set_of_group[gi];
    mesh.groups.push_back(std::move(group));
  }
  out.materials = std::move(materials);
  out.unread_chunks = std::move(p.unread);
  return {};
}

} // namespace bw::content_converter
