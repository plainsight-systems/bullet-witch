#include "bw/rendering/shaders/artifact.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Responsibility: the writer, loader and naming functions of
// bw/rendering/shaders/artifact.hpp. Both directions work on bytes; nothing
// here opens a file.
//
// Contract, writer: validates the inputs in the header's ArtifactWriteError
// order, then writes header, reflection, code and trailer in the header's
// byte format, little-endian, into `out` (replacing its contents). Nothing is
// written to `out` on a failure. Blocks are written in (kind, slot) order and
// vertex inputs in (location, semantic index, semantic name) order, so equal
// inputs in any order give identical bytes; trailer entries in the order
// given. Checks, in order:
//   InvalidTargetOrStage    header target or stage not an enumerator.
//   StringTooLong           a header, block or semantic string over
//                           kMaxArtifactString (block and input strings are
//                           scanned up to the arrays' capacity, so this check
//                           can precede the count checks without reading past
//                           the arrays).
//   TooManyBlocks           block_count over kMaxReflectedBlocks.
//   TooManyVertexInputs     vertex_input_count over kMaxVertexInputs.
//   SlotOutOfRange          any block: static slot >= kMaxStaticBlocks, draw
//                           slot other than 0, buffer slot >=
//                           kMaxBufferBlocks, or kind not an enumerator.
//   DuplicateSlot           two blocks with the same (kind, slot).
//   ThreadGroupOnNonCompute a non-zero thread group size, stage not compute.
//   UnknownDescriptorArray  a descriptor_arrays bit >= kDescriptorArrayCount.
//   EmptyCode               the code block is empty.
//   TrailerEntryTooLarge    one entry's bytes over the u32 size field.
//   ImageTooLarge           the code block, the trailer or the whole image
//                           over the u32 size fields.
// Every image the writer produces passes loader checks 1-10.
//
// Contract, loader: never reads outside `bytes`, allocates nothing, returns
// views into `bytes`. The header's fifteen checks, in its order, each failure
// naming the compared values in expected/actual and byte_offset where the
// loader stopped:
//    1 Truncated            fewer than the 76 fixed header bytes.
//    2 BadMagic             magic != 'BWSH'.
//    3 UnsupportedVersion   format_version != 1.
//    4 SizeMismatch         total_size != bytes.size(). Cutting bytes off a
//                           valid image is reported here, with both sizes.
//    5 UnknownTarget, UnknownStage.
//    6 MalformedHeader      header_size or reflection_size not a multiple of
//                           4, code_size zero, header_size below 76, the
//                           reserved u16 non-zero.
//    7 Truncated            the four section sizes sum past total_size (a
//                           section runs off the end); MalformedHeader when
//                           they sum to less.
//    8 MalformedHeader      the shader name or entry point over
//                           kMaxArtifactString or past header_size; the
//                           format's zero padding non-zero; header_size not
//                           ending where the strings and padding end.
//    9 MalformedReflection  the header's list (counts over capacity, strings
//                           past the section or over the bound, kind or slot
//                           out of range, a descriptor_arrays bit at or above
//                           kDescriptorArrayCount, a non-zero thread group on
//                           a non-compute stage, two blocks with one (kind,
//                           slot)), and the byte format's own rules under the
//                           same code: reserved and padding bytes zero, blocks
//                           strictly ascending by (kind, slot) (which is how
//                           duplicates are found), inputs non-decreasing by
//                           (location, semantic index, name), no bytes left.
//   10 TrailerCorrupt       trailer_size under 4; closing u32 != trailer_size;
//                           the key/size/bytes walk not landing on the closer.
//   11 PermutationOutOfRange  permutation >= permutation_count, when non-zero.
//   12 RootLayoutMismatch   artifact root layout hash != the expected layout's.
//                           A null expected layout fails here too (expected 0):
//                           the check is never skipped.
//   13 UnknownBlock         a reflected block name absent from running_blocks
//                           (null entries of running_blocks are ignored).
//   14 LayoutHashMismatch   the running block's hash differs.
//   15 SlotMismatch         a static block's slot differs from the root
//                           layout's slot for that name; expected is
//                           kMaxStaticBlocks when the layout has no slot for it.
// Artifact::trailer is the whole trailer section, entries and closing length.
//
// Algorithm, load: read the trailer's closing length, require it within the
// data, then walk key, size, value records up to the closing length. The
// header and reflection checks come before it, as the header says.
//
// Design and cost: one pass over header, reflection and trailer with a
// bounds-checked cursor; the code block is never touched. Integers are
// assembled from bytes with shifts, so there is no alignment or host-endian
// assumption and no type punning. Strings are viewed in place: the only
// pointer conversion is std::byte to char, which may alias any object and has
// alignment 1. The writer computes every size before writing and reserves the
// exact image once.
//
// Retrieved rules: SL.con.3 (every read is preceded by a remaining-length
// check; views are cut with span::subspan), ES.48 (no casts of pointers to
// wider types; integers are built from bytes), SL.str.2 (header and
// reflection strings are string_views into the caller's bytes), E.27 (each
// failure is a named code with its compared values; no exceptions).
// Performance: MEM.9 (the load path allocates nothing; the decoded reflection
// lives in fixed arrays), EMB.2 (fixed capacity arrays sized by
// kMaxReflectedBlocks and kMaxVertexInputs; overflow is a named error, not
// growth), CDSA.9 (insertion sort over an index array of at most 16 entries;
// running-block lookup is a linear scan).
namespace bw::shaders {
namespace {

// Fixed header field offsets, from the header's byte format.
constexpr std::size_t kOffsetMagic = 0;
constexpr std::size_t kOffsetVersion = 4;
constexpr std::size_t kOffsetTotalSize = 8;
constexpr std::size_t kOffsetHeaderSize = 12;
constexpr std::size_t kOffsetTarget = 16;
constexpr std::size_t kOffsetStage = 17;
constexpr std::size_t kOffsetReserved = 18;
constexpr std::size_t kOffsetPermutation = 20;
constexpr std::size_t kOffsetInputHash = 24;
constexpr std::size_t kOffsetRootLayoutHash = 56;
constexpr std::size_t kOffsetReflectionSize = 64;
constexpr std::size_t kOffsetCodeSize = 68;
constexpr std::size_t kOffsetTrailerSize = 72;
constexpr std::size_t kFixedHeaderBytes = 76;
constexpr std::size_t kTrailerCloserBytes = 4;
constexpr std::size_t kTrailerEntryPrefixBytes = 5;
constexpr std::uint32_t kKnownDescriptorArrays = (1U << kDescriptorArrayCount) - 1U;
constexpr std::uint64_t kU32Max = std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] constexpr std::size_t padded4(std::size_t value) noexcept {
  return (value + 3U) & ~std::size_t{3U};
}

// ---- writing ---------------------------------------------------------------

class ByteSink {
public:
  explicit ByteSink(std::vector<std::byte>& out) : out_(out) {}
  void u8(std::uint8_t value) { out_.push_back(static_cast<std::byte>(value)); }
  void u16(std::uint16_t value) {
    u8(static_cast<std::uint8_t>(value & 0xFFU));
    u8(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
  }
  void u32(std::uint32_t value) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
      u8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
  }
  void u64(std::uint64_t value) {
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
      u8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
  }
  void bytes(std::span<const std::byte> data) { out_.insert(out_.end(), data.begin(), data.end()); }
  void chars(std::string_view text) {
    for (const char c : text) {
      u8(static_cast<std::uint8_t>(c));
    }
  }
  // Zero bytes up to a multiple of 4 counted from `section_start`.
  void pad_to_4(std::size_t section_start) {
    while ((out_.size() - section_start) % 4U != 0U) {
      u8(0);
    }
  }
  [[nodiscard]] std::size_t size() const noexcept { return out_.size(); }

private:
  std::vector<std::byte>& out_;
};

[[nodiscard]] bool block_before(const ReflectedBlock& a, const ReflectedBlock& b) noexcept {
  if (a.kind != b.kind) {
    return static_cast<std::uint8_t>(a.kind) < static_cast<std::uint8_t>(b.kind);
  }
  return a.slot < b.slot;
}

[[nodiscard]] bool input_before(const VertexInput& a, const VertexInput& b) noexcept {
  if (a.location != b.location) {
    return a.location < b.location;
  }
  if (a.semantic_index != b.semantic_index) {
    return a.semantic_index < b.semantic_index;
  }
  return a.semantic_name < b.semantic_name;
}

// Insertion sort of an index array (CDSA.9: at most 16 entries).
template <class T, std::size_t N, class Before>
[[nodiscard]] std::array<std::uint32_t, N> sorted_order(std::span<const T> items, Before before) {
  std::array<std::uint32_t, N> order{};
  for (std::uint32_t i = 0; i < items.size(); ++i) {
    order[i] = i;
  }
  for (std::size_t i = 1; i < items.size(); ++i) {
    for (std::size_t j = i; j > 0 && before(items[order[j]], items[order[j - 1]]); --j) {
      const std::uint32_t swap = order[j - 1];
      order[j - 1] = order[j];
      order[j] = swap;
    }
  }
  return order;
}

[[nodiscard]] bool known_target(ShaderTarget target) noexcept {
  switch (target) {
  case ShaderTarget::Direct3D12:
  case ShaderTarget::Vulkan:
  case ShaderTarget::Metal:
    return true;
  }
  return false;
}

[[nodiscard]] bool known_stage(ShaderStage stage) noexcept {
  switch (stage) {
  case ShaderStage::Vertex:
  case ShaderStage::Pixel:
  case ShaderStage::Compute:
    return true;
  }
  return false;
}

[[nodiscard]] bool block_fits_root_layout(const ReflectedBlock& block) noexcept {
  switch (block.kind) {
  case BlockSlotKind::Static:
    return block.slot < kMaxStaticBlocks;
  case BlockSlotKind::Draw:
    return block.slot == 0;
  case BlockSlotKind::Buffer:
    return block.slot < kMaxBufferBlocks;
  }
  return false;
}

[[nodiscard]] std::size_t header_section_size(const ArtifactHeader& header) noexcept {
  return padded4(kFixedHeaderBytes + 4U + header.shader_name.size() + 4U +
                 header.entry_point.size());
}

[[nodiscard]] std::size_t reflection_section_size(std::span<const ReflectedBlock> blocks,
                                                  std::span<const VertexInput> inputs) noexcept {
  // descriptor_arrays, root constant words, thread group, block count.
  std::size_t size = 4U + 4U + 12U + 4U;
  for (const ReflectedBlock& block : blocks) {
    size += 4U + 8U + padded4(4U + block.name.size());
  }
  size += 4U;
  for (const VertexInput& input : inputs) {
    size += 4U + 4U + padded4(4U + input.semantic_name.size());
  }
  return size;
}

// ---- reading ---------------------------------------------------------------

[[nodiscard]] std::uint8_t load_u8(std::span<const std::byte> bytes, std::size_t at) noexcept {
  return std::to_integer<std::uint8_t>(bytes[at]);
}

[[nodiscard]] std::uint32_t load_u32(std::span<const std::byte> bytes, std::size_t at) noexcept {
  std::uint32_t value = 0;
  for (unsigned i = 0; i < 4U; ++i) {
    value |= std::to_integer<std::uint32_t>(bytes[at + i]) << (8U * i);
  }
  return value;
}

[[nodiscard]] std::uint64_t load_u64(std::span<const std::byte> bytes, std::size_t at) noexcept {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8U; ++i) {
    value |= std::to_integer<std::uint64_t>(bytes[at + i]) << (8U * i);
  }
  return value;
}

// View `length` bytes at `at` as characters. Precondition: in bounds.
[[nodiscard]] std::string_view view_chars(std::span<const std::byte> bytes, std::size_t at,
                                          std::size_t length) noexcept {
  const std::span<const std::byte> cut = bytes.subspan(at, length);
  // std::byte to char: char may alias any object and has alignment 1.
  return {reinterpret_cast<const char*>(cut.data()), cut.size()};
}

// A bounds-checked forward reader over [start, end) of `bytes`. Padding is
// counted from `origin`. Every read returns false, without reading, when
// fewer bytes remain.
class Cursor {
public:
  struct Range {
    std::size_t origin{};
    std::size_t start{};
    std::size_t end{};
  };
  Cursor(std::span<const std::byte> bytes, Range range) noexcept
      : bytes_(bytes), origin_(range.origin), position_(range.start), end_(range.end) {}
  [[nodiscard]] std::size_t position() const noexcept { return position_; }
  [[nodiscard]] std::size_t remaining() const noexcept { return end_ - position_; }
  [[nodiscard]] bool u8(std::uint8_t& value) noexcept {
    if (remaining() < 1U) {
      return false;
    }
    value = load_u8(bytes_, position_);
    position_ += 1U;
    return true;
  }
  [[nodiscard]] bool u16(std::uint16_t& value) noexcept {
    if (remaining() < 2U) {
      return false;
    }
    value =
        static_cast<std::uint16_t>(static_cast<unsigned>(load_u8(bytes_, position_)) |
                                   (static_cast<unsigned>(load_u8(bytes_, position_ + 1U)) << 8U));
    position_ += 2U;
    return true;
  }
  [[nodiscard]] bool u32(std::uint32_t& value) noexcept {
    if (remaining() < 4U) {
      return false;
    }
    value = load_u32(bytes_, position_);
    position_ += 4U;
    return true;
  }
  [[nodiscard]] bool u64(std::uint64_t& value) noexcept {
    if (remaining() < 8U) {
      return false;
    }
    value = load_u64(bytes_, position_);
    position_ += 8U;
    return true;
  }
  // u32 length, at most kMaxArtifactString, then that many bytes.
  [[nodiscard]] bool string(std::string_view& text) noexcept {
    std::uint32_t length = 0;
    if (!u32(length) || length > kMaxArtifactString || remaining() < length) {
      return false;
    }
    text = view_chars(bytes_, position_, length);
    position_ += length;
    return true;
  }
  // Zero bytes up to a multiple of 4 from the origin.
  [[nodiscard]] bool zero_pad_to_4() noexcept {
    while ((position_ - origin_) % 4U != 0U) {
      std::uint8_t pad = 0;
      if (!u8(pad) || pad != 0U) {
        return false;
      }
    }
    return true;
  }

private:
  std::span<const std::byte> bytes_;
  std::size_t origin_;
  std::size_t position_;
  std::size_t end_;
};

[[nodiscard]] LoadError fail(ArtifactError code, std::uint64_t expected, std::uint64_t actual,
                             std::size_t byte_offset) noexcept {
  return LoadError{.block_name = {},
                   .expected = expected,
                   .actual = actual,
                   .byte_offset = static_cast<std::uint32_t>(byte_offset),
                   .code = code};
}

struct Sections {
  std::size_t header{};
  std::size_t reflection{};
  std::size_t code{};
  std::size_t trailer{};
};

// Check 6: section sizes and the reserved field.
[[nodiscard]] LoadError check_header_fields(std::span<const std::byte> bytes,
                                            const Sections& sections) noexcept {
  if (sections.header % 4U != 0U) {
    return fail(ArtifactError::MalformedHeader, 0, sections.header % 4U, kOffsetHeaderSize);
  }
  if (sections.reflection % 4U != 0U) {
    return fail(ArtifactError::MalformedHeader, 0, sections.reflection % 4U, kOffsetReflectionSize);
  }
  if (sections.code == 0U) {
    return fail(ArtifactError::MalformedHeader, 1, 0, kOffsetCodeSize);
  }
  if (sections.header < kFixedHeaderBytes) {
    return fail(ArtifactError::MalformedHeader, kFixedHeaderBytes, sections.header,
                kOffsetHeaderSize);
  }
  const std::uint64_t reserved = std::uint64_t{load_u8(bytes, kOffsetReserved)} |
                                 (std::uint64_t{load_u8(bytes, kOffsetReserved + 1U)} << 8U);
  if (reserved != 0U) {
    return fail(ArtifactError::MalformedHeader, 0, reserved, kOffsetReserved);
  }
  return LoadError{};
}

// Check 8: the strings and padding inside header_size.
[[nodiscard]] LoadError read_header_strings(std::span<const std::byte> bytes,
                                            const Sections& sections,
                                            ArtifactHeader& header) noexcept {
  Cursor cursor{bytes, {.origin = 0, .start = kFixedHeaderBytes, .end = sections.header}};
  if (!cursor.string(header.shader_name) || !cursor.string(header.entry_point) ||
      !cursor.zero_pad_to_4()) {
    return fail(ArtifactError::MalformedHeader, sections.header, cursor.position(),
                cursor.position());
  }
  if (cursor.remaining() != 0U) {
    return fail(ArtifactError::MalformedHeader, cursor.position(), sections.header,
                cursor.position());
  }
  return LoadError{};
}

// Check 9: the reflection section, decoded into the fixed arrays.
// `block_offsets` receives each block record's byte offset for later errors.
[[nodiscard]] LoadError
read_reflection(std::span<const std::byte> bytes, const Sections& sections, ShaderStage stage,
                Reflection& reflection,
                std::array<std::size_t, kMaxReflectedBlocks>& block_offsets) noexcept {
  const std::size_t begin = sections.header;
  const std::size_t end = begin + sections.reflection;
  Cursor cursor{bytes, {.origin = begin, .start = begin, .end = end}};
  const auto malformed = [&cursor](std::uint64_t expected, std::uint64_t actual) {
    return fail(ArtifactError::MalformedReflection, expected, actual, cursor.position());
  };
  // reflection_size is a multiple of 4 (check 6).
  if (!cursor.u32(reflection.descriptor_arrays)) {
    return malformed(4, cursor.remaining());
  }
  if ((reflection.descriptor_arrays & ~kKnownDescriptorArrays) != 0U) {
    return malformed(0, reflection.descriptor_arrays & ~kKnownDescriptorArrays);
  }
  if (!cursor.u32(reflection.root_constant_words) || !cursor.u32(reflection.thread_group_size[0]) ||
      !cursor.u32(reflection.thread_group_size[1]) ||
      !cursor.u32(reflection.thread_group_size[2])) {
    return malformed(16, cursor.remaining());
  }
  if (stage != ShaderStage::Compute) {
    for (const std::uint32_t size : reflection.thread_group_size) {
      if (size != 0U) {
        return malformed(0, size);
      }
    }
  }
  if (!cursor.u32(reflection.block_count)) {
    return malformed(4, cursor.remaining());
  }
  if (reflection.block_count > kMaxReflectedBlocks) {
    return malformed(kMaxReflectedBlocks, reflection.block_count);
  }
  for (std::uint32_t i = 0; i < reflection.block_count; ++i) {
    block_offsets[i] = cursor.position();
    ReflectedBlock& block = reflection.blocks[i];
    std::uint8_t kind = 0;
    std::uint8_t slot = 0;
    std::uint16_t zero = 0;
    if (!cursor.u8(kind) || !cursor.u8(slot) || !cursor.u16(zero) ||
        !cursor.u64(block.layout_hash.value) || !cursor.string(block.name) ||
        !cursor.zero_pad_to_4()) {
      return malformed(sections.reflection, cursor.position() - begin);
    }
    if (zero != 0U) {
      return malformed(0, zero);
    }
    if (kind == static_cast<std::uint8_t>(BlockSlotKind::Static)) {
      block.kind = BlockSlotKind::Static;
      if (slot >= kMaxStaticBlocks) {
        return malformed(kMaxStaticBlocks, slot);
      }
    } else if (kind == static_cast<std::uint8_t>(BlockSlotKind::Draw)) {
      block.kind = BlockSlotKind::Draw;
      if (slot != 0U) {
        return malformed(0, slot);
      }
    } else if (kind == static_cast<std::uint8_t>(BlockSlotKind::Buffer)) {
      block.kind = BlockSlotKind::Buffer;
      if (slot >= kMaxBufferBlocks) {
        return malformed(kMaxBufferBlocks, slot);
      }
    } else {
      return malformed(static_cast<std::uint8_t>(BlockSlotKind::Buffer), kind);
    }
    block.slot = slot;
    if (i > 0 && !block_before(reflection.blocks[i - 1], block)) {
      return malformed(reflection.blocks[i - 1].slot, block.slot);
    }
  }
  if (!cursor.u32(reflection.vertex_input_count)) {
    return malformed(4, cursor.remaining());
  }
  if (reflection.vertex_input_count > kMaxVertexInputs) {
    return malformed(kMaxVertexInputs, reflection.vertex_input_count);
  }
  for (std::uint32_t i = 0; i < reflection.vertex_input_count; ++i) {
    VertexInput& input = reflection.vertex_inputs[i];
    if (!cursor.u32(input.location) || !cursor.u32(input.semantic_index) ||
        !cursor.string(input.semantic_name) || !cursor.zero_pad_to_4()) {
      return malformed(sections.reflection, cursor.position() - begin);
    }
    // Non-decreasing: the writer keeps equal inputs, so equal is well formed.
    if (i > 0 && input_before(input, reflection.vertex_inputs[i - 1])) {
      return malformed(reflection.vertex_inputs[i - 1].location, input.location);
    }
  }
  if (cursor.remaining() != 0U) {
    return malformed(0, cursor.remaining());
  }
  return LoadError{};
}

// Check 10: the trailer is closed by its own length and the records walk lands
// exactly on that closer. Unknown keys are accepted.
[[nodiscard]] LoadError check_trailer(std::span<const std::byte> trailer,
                                      std::size_t trailer_offset) noexcept {
  if (trailer.size() < kTrailerCloserBytes) {
    return fail(ArtifactError::TrailerCorrupt, kTrailerCloserBytes, trailer.size(), trailer_offset);
  }
  const std::size_t closer_at = trailer.size() - kTrailerCloserBytes;
  const std::uint32_t total = load_u32(trailer, closer_at);
  if (total != trailer.size()) {
    return fail(ArtifactError::TrailerCorrupt, trailer.size(), total, trailer_offset + closer_at);
  }
  std::size_t position = 0;
  while (position < closer_at) {
    if (closer_at - position < kTrailerEntryPrefixBytes) {
      return fail(ArtifactError::TrailerCorrupt, kTrailerEntryPrefixBytes, closer_at - position,
                  trailer_offset + position);
    }
    const std::uint32_t size = load_u32(trailer, position + 1U);
    const std::size_t room = closer_at - position - kTrailerEntryPrefixBytes;
    if (size > room) {
      return fail(ArtifactError::TrailerCorrupt, room, size, trailer_offset + position);
    }
    position += kTrailerEntryPrefixBytes + size;
  }
  return LoadError{};
}

[[nodiscard]] const BlockLayout* find_running_block(std::span<const BlockLayout* const> running,
                                                    std::string_view name) noexcept {
  for (const BlockLayout* layout : running) {
    if (layout != nullptr && layout->name == name) {
      return layout;
    }
  }
  return nullptr;
}

void append_decimal(std::uint32_t value, std::string& out) {
  std::array<char, 16> buffer{};
  const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  // A uint32 always fits; to_chars is locale-independent.
  if (error == std::errc{}) {
    out.append(buffer.data(), end);
  }
}

} // namespace

ArtifactWriteError write_artifact(const ArtifactHeader& header, const Reflection& reflection,
                                  std::span<const std::byte> code,
                                  std::span<const TrailerEntry> trailer,
                                  std::vector<std::byte>& out) {
  // Validation, in the header's order (file contract).
  if (!known_target(header.target) || !known_stage(header.stage)) {
    return ArtifactWriteError::InvalidTargetOrStage;
  }
  if (header.shader_name.size() > kMaxArtifactString ||
      header.entry_point.size() > kMaxArtifactString) {
    return ArtifactWriteError::StringTooLong;
  }
  const std::size_t scanned_blocks =
      reflection.block_count < kMaxReflectedBlocks ? reflection.block_count : kMaxReflectedBlocks;
  for (std::size_t i = 0; i < scanned_blocks; ++i) {
    if (reflection.blocks[i].name.size() > kMaxArtifactString) {
      return ArtifactWriteError::StringTooLong;
    }
  }
  const std::size_t scanned_inputs = reflection.vertex_input_count < kMaxVertexInputs
                                         ? reflection.vertex_input_count
                                         : kMaxVertexInputs;
  for (std::size_t i = 0; i < scanned_inputs; ++i) {
    if (reflection.vertex_inputs[i].semantic_name.size() > kMaxArtifactString) {
      return ArtifactWriteError::StringTooLong;
    }
  }
  if (reflection.block_count > kMaxReflectedBlocks) {
    return ArtifactWriteError::TooManyBlocks;
  }
  if (reflection.vertex_input_count > kMaxVertexInputs) {
    return ArtifactWriteError::TooManyVertexInputs;
  }
  const std::span<const ReflectedBlock> blocks = reflection.bound_blocks();
  const std::span<const VertexInput> inputs = reflection.inputs();
  for (const ReflectedBlock& block : blocks) {
    if (!block_fits_root_layout(block)) {
      return ArtifactWriteError::SlotOutOfRange;
    }
  }
  const auto block_order = sorted_order<ReflectedBlock, kMaxReflectedBlocks>(blocks, block_before);
  for (std::size_t i = 1; i < blocks.size(); ++i) {
    if (!block_before(blocks[block_order[i - 1]], blocks[block_order[i]])) {
      return ArtifactWriteError::DuplicateSlot;
    }
  }
  if (header.stage != ShaderStage::Compute) {
    for (const std::uint32_t size : reflection.thread_group_size) {
      if (size != 0U) {
        return ArtifactWriteError::ThreadGroupOnNonCompute;
      }
    }
  }
  if ((reflection.descriptor_arrays & ~kKnownDescriptorArrays) != 0U) {
    return ArtifactWriteError::UnknownDescriptorArray;
  }
  if (code.empty()) {
    return ArtifactWriteError::EmptyCode;
  }
  std::uint64_t trailer_size = kTrailerCloserBytes;
  for (const TrailerEntry& entry : trailer) {
    if (entry.bytes.size() > kU32Max) {
      return ArtifactWriteError::TrailerEntryTooLarge;
    }
    trailer_size += kTrailerEntryPrefixBytes + entry.bytes.size();
  }
  const std::size_t header_size = header_section_size(header);
  const std::size_t reflection_size = reflection_section_size(blocks, inputs);
  const std::uint64_t total =
      std::uint64_t{header_size} + reflection_size + code.size() + trailer_size;
  if (code.size() > kU32Max || trailer_size > kU32Max || total > kU32Max) {
    return ArtifactWriteError::ImageTooLarge;
  }

  out.clear();
  out.reserve(static_cast<std::size_t>(total));
  ByteSink sink{out};
  // Header.
  sink.u32(kArtifactMagic);
  sink.u32(kArtifactFormatVersion);
  sink.u32(static_cast<std::uint32_t>(total));
  sink.u32(static_cast<std::uint32_t>(header_size));
  sink.u8(static_cast<std::uint8_t>(header.target));
  sink.u8(static_cast<std::uint8_t>(header.stage));
  sink.u16(0);
  sink.u32(header.permutation.value);
  for (const std::uint8_t byte : header.input_hash) {
    sink.u8(byte);
  }
  sink.u64(header.root_layout_hash.value);
  sink.u32(static_cast<std::uint32_t>(reflection_size));
  sink.u32(static_cast<std::uint32_t>(code.size()));
  sink.u32(static_cast<std::uint32_t>(trailer_size));
  sink.u32(static_cast<std::uint32_t>(header.shader_name.size()));
  sink.chars(header.shader_name);
  sink.u32(static_cast<std::uint32_t>(header.entry_point.size()));
  sink.chars(header.entry_point);
  sink.pad_to_4(0);
  // Reflection, blocks in (kind, slot) order and inputs in (location,
  // semantic index, name) order.
  const std::size_t reflection_start = sink.size();
  sink.u32(reflection.descriptor_arrays);
  sink.u32(reflection.root_constant_words);
  for (const std::uint32_t size : reflection.thread_group_size) {
    sink.u32(size);
  }
  sink.u32(reflection.block_count);
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    const ReflectedBlock& block = blocks[block_order[i]];
    sink.u8(static_cast<std::uint8_t>(block.kind));
    sink.u8(static_cast<std::uint8_t>(block.slot));
    sink.u16(0);
    sink.u64(block.layout_hash.value);
    sink.u32(static_cast<std::uint32_t>(block.name.size()));
    sink.chars(block.name);
    sink.pad_to_4(reflection_start);
  }
  const auto input_order = sorted_order<VertexInput, kMaxVertexInputs>(inputs, input_before);
  sink.u32(reflection.vertex_input_count);
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    const VertexInput& input = inputs[input_order[i]];
    sink.u32(input.location);
    sink.u32(input.semantic_index);
    sink.u32(static_cast<std::uint32_t>(input.semantic_name.size()));
    sink.chars(input.semantic_name);
    sink.pad_to_4(reflection_start);
  }
  // Code, uncompressed.
  sink.bytes(code);
  // Trailer: key, size, bytes per entry, closed by its own length.
  for (const TrailerEntry& entry : trailer) {
    sink.u8(static_cast<std::uint8_t>(entry.key));
    sink.u32(static_cast<std::uint32_t>(entry.bytes.size()));
    sink.bytes(entry.bytes);
  }
  sink.u32(static_cast<std::uint32_t>(trailer_size));
  return ArtifactWriteError::None;
}

ArtifactLoadResult load_artifact(std::span<const std::byte> bytes,
                                 const LoadExpectations& expectations) noexcept {
  // 1. Fixed header present.
  if (bytes.size() < kFixedHeaderBytes) {
    return ArtifactLoadResult::failure(
        fail(ArtifactError::Truncated, kFixedHeaderBytes, bytes.size(), bytes.size()));
  }
  // 2. Magic.
  const std::uint32_t magic = load_u32(bytes, kOffsetMagic);
  if (magic != kArtifactMagic) {
    return ArtifactLoadResult::failure(
        fail(ArtifactError::BadMagic, kArtifactMagic, magic, kOffsetMagic));
  }
  // 3. Version.
  const std::uint32_t version = load_u32(bytes, kOffsetVersion);
  if (version != kArtifactFormatVersion) {
    return ArtifactLoadResult::failure(
        fail(ArtifactError::UnsupportedVersion, kArtifactFormatVersion, version, kOffsetVersion));
  }
  // 4. Declared total equals the bytes given.
  const std::uint32_t total = load_u32(bytes, kOffsetTotalSize);
  if (total != bytes.size()) {
    return ArtifactLoadResult::failure(
        fail(ArtifactError::SizeMismatch, total, bytes.size(), kOffsetTotalSize));
  }
  // 5. Target and stage known.
  Artifact artifact{};
  const std::uint8_t target = load_u8(bytes, kOffsetTarget);
  switch (target) {
  case static_cast<std::uint8_t>(ShaderTarget::Direct3D12):
  case static_cast<std::uint8_t>(ShaderTarget::Vulkan):
  case static_cast<std::uint8_t>(ShaderTarget::Metal):
    artifact.header.target = static_cast<ShaderTarget>(target);
    break;
  default:
    return ArtifactLoadResult::failure(fail(ArtifactError::UnknownTarget,
                                            static_cast<std::uint8_t>(ShaderTarget::Metal), target,
                                            kOffsetTarget));
  }
  const std::uint8_t stage = load_u8(bytes, kOffsetStage);
  switch (stage) {
  case static_cast<std::uint8_t>(ShaderStage::Vertex):
  case static_cast<std::uint8_t>(ShaderStage::Pixel):
  case static_cast<std::uint8_t>(ShaderStage::Compute):
    artifact.header.stage = static_cast<ShaderStage>(stage);
    break;
  default:
    return ArtifactLoadResult::failure(fail(ArtifactError::UnknownStage,
                                            static_cast<std::uint8_t>(ShaderStage::Compute), stage,
                                            kOffsetStage));
  }
  // 6. Section sizes and the reserved field.
  const Sections sections{.header = load_u32(bytes, kOffsetHeaderSize),
                          .reflection = load_u32(bytes, kOffsetReflectionSize),
                          .code = load_u32(bytes, kOffsetCodeSize),
                          .trailer = load_u32(bytes, kOffsetTrailerSize)};
  if (const LoadError error = check_header_fields(bytes, sections);
      error.code != ArtifactError::None) {
    return ArtifactLoadResult::failure(error);
  }
  // 7. Sections sum exactly to the total: past it is Truncated, short of it
  // MalformedHeader.
  const std::uint64_t section_sum =
      std::uint64_t{sections.header} + sections.reflection + sections.code + sections.trailer;
  if (section_sum > total) {
    return ArtifactLoadResult::failure(
        fail(ArtifactError::Truncated, section_sum, total, kOffsetHeaderSize));
  }
  if (section_sum < total) {
    return ArtifactLoadResult::failure(
        fail(ArtifactError::MalformedHeader, total, section_sum, kOffsetHeaderSize));
  }
  // 8. Header strings.
  if (const LoadError error = read_header_strings(bytes, sections, artifact.header);
      error.code != ArtifactError::None) {
    return ArtifactLoadResult::failure(error);
  }
  artifact.header.permutation = PermutationId{load_u32(bytes, kOffsetPermutation)};
  for (std::size_t i = 0; i < kInputHashBytes; ++i) {
    artifact.header.input_hash[i] = load_u8(bytes, kOffsetInputHash + i);
  }
  artifact.header.root_layout_hash = LayoutHash{load_u64(bytes, kOffsetRootLayoutHash)};
  // 9. Reflection.
  std::array<std::size_t, kMaxReflectedBlocks> block_offsets{};
  if (const LoadError error = read_reflection(bytes, sections, artifact.header.stage,
                                              artifact.reflection, block_offsets);
      error.code != ArtifactError::None) {
    return ArtifactLoadResult::failure(error);
  }
  const std::size_t code_offset = sections.header + sections.reflection;
  const std::size_t trailer_offset = code_offset + sections.code;
  artifact.code = bytes.subspan(code_offset, sections.code);
  artifact.trailer = bytes.subspan(trailer_offset, sections.trailer);
  // 10. Trailer.
  if (const LoadError error = check_trailer(artifact.trailer, trailer_offset);
      error.code != ArtifactError::None) {
    return ArtifactLoadResult::failure(error);
  }
  // 11. Permutation range, when the caller knows the count.
  if (expectations.permutation_count != 0U &&
      artifact.header.permutation.value >= expectations.permutation_count) {
    return ArtifactLoadResult::failure(fail(ArtifactError::PermutationOutOfRange,
                                            expectations.permutation_count,
                                            artifact.header.permutation.value, kOffsetPermutation));
  }
  // 12. Root layout. A missing expected layout fails; it is not a skip.
  const std::uint64_t expected_root =
      expectations.root_layout == nullptr ? 0U : root_layout_hash(*expectations.root_layout).value;
  if (expectations.root_layout == nullptr ||
      artifact.header.root_layout_hash.value != expected_root) {
    return ArtifactLoadResult::failure(fail(ArtifactError::RootLayoutMismatch, expected_root,
                                            artifact.header.root_layout_hash.value,
                                            kOffsetRootLayoutHash));
  }
  // 13-15. Every reflected block against the running declarations.
  for (std::uint32_t i = 0; i < artifact.reflection.block_count; ++i) {
    const ReflectedBlock& block = artifact.reflection.blocks[i];
    const BlockLayout* running = find_running_block(expectations.running_blocks, block.name);
    if (running == nullptr) {
      LoadError error =
          fail(ArtifactError::UnknownBlock, 0, block.layout_hash.value, block_offsets[i]);
      error.block_name = block.name;
      return ArtifactLoadResult::failure(error);
    }
    if (running->hash != block.layout_hash) {
      LoadError error = fail(ArtifactError::LayoutHashMismatch, running->hash.value,
                             block.layout_hash.value, block_offsets[i]);
      error.block_name = block.name;
      return ArtifactLoadResult::failure(error);
    }
    if (block.kind == BlockSlotKind::Static) {
      const std::optional<std::uint32_t> slot =
          find_static_slot(*expectations.root_layout, block.name);
      if (!slot.has_value() || *slot != block.slot) {
        LoadError error = fail(ArtifactError::SlotMismatch, slot.value_or(kMaxStaticBlocks),
                               block.slot, block_offsets[i]);
        error.block_name = block.name;
        return ArtifactLoadResult::failure(error);
      }
    }
  }
  return ArtifactLoadResult::success(artifact);
}

std::optional<std::span<const std::byte>> find_trailer_entry(std::span<const std::byte> trailer,
                                                             TrailerKey key) noexcept {
  // The same walk as the loader's check; any malformation gives no value.
  if (check_trailer(trailer, 0).code != ArtifactError::None) {
    return std::nullopt;
  }
  const std::size_t closer_at = trailer.size() - kTrailerCloserBytes;
  std::size_t position = 0;
  while (position < closer_at) {
    const std::uint8_t entry_key = load_u8(trailer, position);
    const std::uint32_t size = load_u32(trailer, position + 1U);
    if (entry_key == static_cast<std::uint8_t>(key)) {
      return trailer.subspan(position + kTrailerEntryPrefixBytes, size);
    }
    position += kTrailerEntryPrefixBytes + size;
  }
  return std::nullopt;
}

BindingUse derive_binding_use(const Reflection& reflection) noexcept {
  BindingUse use{};
  use.root_constant_words = reflection.root_constant_words;
  use.descriptor_arrays = reflection.descriptor_arrays;
  const std::size_t count =
      reflection.block_count < kMaxReflectedBlocks ? reflection.block_count : kMaxReflectedBlocks;
  for (std::size_t i = 0; i < count; ++i) {
    const ReflectedBlock& block = reflection.blocks[i];
    if (block.kind == BlockSlotKind::Draw) {
      use.uses_draw_block = true;
      use.draw_block_hash = block.layout_hash;
    } else if (block.kind == BlockSlotKind::Static && block.slot < kMaxStaticBlocks) {
      // A loaded reflection never holds a slot outside the layout (check 9).
      // A buffer block binds nothing and takes no part in the binding use.
      use.static_slot_mask = static_cast<std::uint8_t>(use.static_slot_mask | (1U << block.slot));
    }
  }
  return use;
}

std::string_view stage_suffix(ShaderStage stage) noexcept {
  switch (stage) {
  case ShaderStage::Vertex:
    return "vs";
  case ShaderStage::Pixel:
    return "ps";
  case ShaderStage::Compute:
    return "cs";
  }
  return {};
}

std::string_view target_directory(ShaderTarget target) noexcept {
  switch (target) {
  case ShaderTarget::Direct3D12:
    return "d3d12";
  case ShaderTarget::Vulkan:
    return "vulkan";
  case ShaderTarget::Metal:
    return "metal";
  }
  return {};
}

std::string artifact_file_name(std::string_view shader_name, PermutationId permutation,
                               ShaderStage stage) {
  std::string name;
  name.reserve(shader_name.size() + 24U);
  name += shader_name;
  name += '.';
  append_decimal(permutation.value, name);
  name += '.';
  name += stage_suffix(stage);
  name += ".bwsh";
  return name;
}

} // namespace bw::shaders
