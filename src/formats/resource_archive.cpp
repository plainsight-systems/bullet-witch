// Inline PNG extent extension: checked chunks recover replacement size while
// first/second payload metadata remains intact. Design and implementation MCP
// SL.con.3 (#rsl-bounds) bounds every chunk; GPU.1 is applied by the consuming
// image adapter, while MEM.9 keeps this traversal allocation-free. See
// research/engine/subsystems/frontend-presentation.md for corpus evidence.
// A1: resource archive decoding (CPR1) (docs/resources.md). parse owns bytes
// and throws Error for malformed/unsupported input. Metadata/spans borrow until
// owner mutation, move or destruction; copies are independent. No I/O; caller
// synchronizes. Algorithm: validate sections, decode arguments/records, return
// bounded views. Cost: O(bytes + decoded metadata); payload O(1). Optimization:
// no payload copy. Design: cpp-guidelines I.1 (#ri-explicit), C.122
// (#rh-separation): explicit ownership and injected file capability.
// cpp-performance COPY.2
// (guidelines/copy-move/COPY.2-mark-move-constructors-noexcept.md): container
// owners retain implicit moves. Implementation: SL.con.3 (#rsl-bounds) checks
// slices before access; COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) keeps owning values rule-of-zero.

// Naming design/implementation consultation: cpp-guidelines P.3 (#rp-what)
// selects role-based names. cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) preserves owning values and implicit
// moves; renaming adds no wrapper, allocation or payload copy.

#include "bw/formats/resource_archive.hpp"

#include <filesystem>
#include <map>
#include <set>

#include "reader.hpp"
namespace bw::resources {
using detail::kByteOrderOffset;
using detail::kNoIndex;
using detail::kWordBytes;
using detail::malformed;
using detail::Reader;
using detail::unsupported;
namespace {
constexpr std::uint64_t kArchiveHeaderBytes = 0x60;
constexpr std::uint32_t kLittleEndianMarker = 0x11223344;
constexpr std::uint64_t kArchiveCompressionOffset = 0x28;
constexpr std::uint64_t kArchiveSecondCompressionOffset = 0x30;
constexpr std::uint64_t kArchiveMetadataSizeOffset = 0x2c;
constexpr std::uint64_t kArchiveSectionsOffset = 0x10;
constexpr std::uint64_t kArchiveRecordBytes = 48;
constexpr std::uint64_t kArchiveArgumentBytes = 12;
constexpr std::size_t kFirstOffsetField = 7;
constexpr std::size_t kFirstSizeField = 8;
constexpr std::size_t kSecondOffsetField = 9;
constexpr std::size_t kSecondSizeField = 10;
constexpr std::uint64_t kArchivePointerBase = 0x28;
// Steam inline PNG replacements retain the old section-size fields. Follow
// checked PNG chunk lengths from the selected pointer, never a byte-pattern
// scan. Archive and encoded-byte budget bound every chunk; libpng later owns
// CRC/deflate/image validation. O(chunks), no allocation or payload copy.
std::optional<std::uint64_t> inline_png_size(const Reader& reader,
                                             std::uint64_t start) {
  constexpr std::array<std::uint8_t, 8> signature{137, 80, 78, 71,
                                                  13,  10, 26, 10};
  constexpr std::uint64_t budget = 64ULL * 1024 * 1024;
  constexpr std::uint64_t chunk_header = 8, chunk_overhead = 12, ihdr_size = 13;
  reader.range(start, 0);
  if (reader.b.size() - start < signature.size()) return std::nullopt;
  const auto input = reader.b.subspan(static_cast<std::size_t>(start));
  if (!std::equal(signature.begin(), signature.end(), input.begin())) {
    return std::nullopt;
  }
  std::uint64_t cursor = signature.size();
  bool has_data = false;
  for (;;) {
    reader.range(start + cursor, chunk_overhead);
    const auto length_bytes =
        reader.b.subspan(static_cast<std::size_t>(start + cursor), kWordBytes);
    std::uint64_t length = 0;
    for (const auto byte : length_bytes) {
      length = (length << detail::kByteBits) | byte;
    }
    if (cursor > budget || length > budget - cursor ||
        chunk_overhead > budget - cursor - length) {
      malformed("Inline PNG exceeds encoded byte budget");
    }
    const auto type = reader.tag(start + cursor + kWordBytes);
    if (cursor == signature.size() && (type != "IHDR" || length != ihdr_size)) {
      malformed("Inline PNG has no valid first IHDR");
    }
    if (!std::all_of(type.begin(), type.end(), [](unsigned char c) {
          return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        })) {
      malformed("Inline PNG chunk type is invalid");
    }
    reader.range(start + cursor + chunk_header, length + kWordBytes);
    cursor += length + chunk_overhead;
    if (type == "IDAT") has_data = true;
    if (type == "IEND") {
      if (length != 0 || !has_data) {
        malformed("Inline PNG terminal chunk is invalid");
      }
      return cursor;
    }
  }
}
}  // namespace
ResourceArchive ResourceArchive::parse(Bytes bytes) {
  ResourceArchive out;
  out.bytes_ = std::move(bytes);
  Reader r{out.bytes_};
  r.range(0, kArchiveHeaderBytes);
  if (r.tag(0) != "CPR1") {
    malformed("Not a CPR1 archive");
  }
  for (std::size_t i = 0; i < out.header_.size(); ++i) {
    out.header_.at(i) = r.u32(4 + i * 4);
  }
  if (r.u32(4) != 1) {
    unsupported("CPR version is not 1");
  }
  if (r.u32(kByteOrderOffset) != kLittleEndianMarker) {
    unsupported("CPR byte order is not little-endian");
  }
  if (r.u32(kArchiveCompressionOffset) ||
      r.u32(kArchiveSecondCompressionOffset)) {
    unsupported("Compressed CPR sections are not implemented");
  }
  std::uint64_t cursor = kArchiveHeaderBytes;
  for (std::size_t i = 0; i < out.sections_.size(); ++i) {
    out.sections_.at(i) = {cursor, r.u32(kArchiveSectionsOffset + i * 4)};
    r.range(cursor, out.sections_.at(i).size);
    cursor += out.sections_.at(i).size;
  }
  const auto [table, args, names, gap, first, second] = out.sections_;
  if (table.size % kArchiveRecordBytes) {
    malformed("CPR record table stride mismatch");
  }
  // 0x41b4a0 uses floor(argument_bytes / 12); trailing alignment bytes are
  // retained.
  if (r.u32(kArchiveMetadataSizeOffset) !=
      table.size + args.size + names.size + gap.size) {
    malformed("CPR metadata size mismatch");
  }
  // The native loader relocates every complete argument triple, even if no
  // record refers to it.
  for (std::uint64_t i = 0; i < args.size / kArchiveArgumentBytes; ++i) {
    auto q = args.offset + i * kArchiveArgumentBytes;
    Argument arg{{r.u32(q), r.u32(q + 4), r.u32(q + 2 * kWordBytes)}, {}, {}};
    arg.name = r.optional_text(names, arg.raw[0]);
    if (arg.name && arg.raw[1] == 2) {
      arg.string_value = r.text(names, arg.raw[2]);
    }
    out.arguments_.push_back(std::move(arg));
  }
  for (std::uint64_t p = table.offset; p < table.offset + table.size;
       p += kArchiveRecordBytes) {
    ResourceArchiveRecord record;
    record.type = r.tag(p);
    for (std::size_t i = 0; i < record.raw.size(); ++i) {
      record.raw.at(i) = r.u32(p + 4 + i * 4);
    }
    const auto& f = record.raw;
    record.name = r.text(names, f[0]);
    record.publication_name = r.optional_text(names, f[1]);
    if (f[3]) {
      r.within(args, static_cast<std::uint64_t>(f[4]) * kArchiveArgumentBytes,
               static_cast<std::uint64_t>(f[3]) * kArchiveArgumentBytes);
      for (std::uint64_t i = 0; i < f[3]; ++i) {
        record.arguments.push_back(
            out.arguments_.at(static_cast<std::size_t>(f[4] + i)));
      }
    }
    if (f[kFirstSizeField]) {
      r.within(first, f[kFirstOffsetField], f[kFirstSizeField]);
      record.first_payload =
          Range{first.offset + f[kFirstOffsetField], f[kFirstSizeField]};
      record.selected_payload = record.first_payload;
    }
    if (f[kSecondSizeField]) {
      r.within(second, f[kSecondOffsetField], f[kSecondSizeField]);
      record.second_payload =
          Range{second.offset + f[kSecondOffsetField], f[kSecondSizeField]};
      if (!record.selected_payload) {
        record.selected_payload = record.second_payload;
      }
    }
    if (f[2] != kNoIndex) {
      record.selected_payload =
          Range{kArchivePointerBase + f[2],
                record.selected_payload ? record.selected_payload->size : 0};
      r.range(record.selected_payload->offset, record.selected_payload->size);
      if (record.type == "DDS ") {
        if (const auto size =
                inline_png_size(r, record.selected_payload->offset)) {
          record.selected_payload->size = *size;
        }
      }
    }
    static constexpr std::array known{"USER", "DDS ", "DDSC", "DXVB",
                                      "DXIB", "DXVS", "DXPS", "CALL"};
    record.known_type =
        std::find(known.begin(), known.end(), record.type) != known.end();
    out.records_.push_back(std::move(record));
  }
  return out;
}
std::span<const std::uint8_t> ResourceArchive::payload(
    std::size_t index) const {
  const auto& range = records_.at(index).selected_payload;
  if (!range) {
    return {};
  }
  return std::span<const std::uint8_t>(bytes_).subspan(
      static_cast<std::size_t>(range->offset),
      static_cast<std::size_t>(range->size));
}
}  // namespace bw::resources
