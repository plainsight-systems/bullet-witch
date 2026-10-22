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
