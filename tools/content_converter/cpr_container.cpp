#include "bw/content_converter/cpr_container.hpp"

#include "bytes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

// Responsibility: the out-of-line part of
// bw/content_converter/cpr_container.hpp: the container parse and the
// lookups over a parsed container.
//
// Contract (the header's): parse_cpr names the first failure in CprError
// order, each check a pass over every record (or every argument triple)
// before the next check runs, so a file that breaks two rules reports the
// one earlier in the enumeration whichever record breaks it. Nothing is read
// before the bytes it lives in are known to be inside the file.
//
// Readings of the header that the checks encode:
//   BadMagic covers both constant words, "CPR1" at 0 and 0x11223344 at 8,
//   and is checked before the version at 4.
//   The call-argument table is not required to be a multiple of 12: it is
//   padded to 16 bytes, so its triples are the floor(bytes / 12) whole
//   12-byte steps it holds, as the header states; a whole step that falls
//   in the zero padding reads as a triple of kind 0 naming the empty string
//   at names offset 0, and passes every check.
//   ArgumentRangeOutOfRange applies to every record whose argument count is
//   not zero, whatever its tag: 16 USER records carry argument triples too.
//   ArgumentValueOutOfRange covers a triple's slot-name offset as well as a
//   kind-2 value: both must name a NUL-terminated string inside the names
//   block. Values of other kinds are kept raw.
//   An Unknown tag has no region the header names, so its payload is empty;
//   its raw tag bytes are kept for the converter's report.
//   A record whose name offset is valid but whose qualified-name offset is
//   0 has no qualified name; offset 0 is the names block's leading NUL.
//   Payload offsets are relative to their region; a payload must end inside
//   its region. The tail block's offset is absolute in the file.
namespace bw::content_converter {

namespace {

constexpr std::array<char, 4> kCprMagic{'C', 'P', 'R', '1'};
constexpr std::uint32_t kByteOrderWord = 0x11223344U;
constexpr std::uint32_t kCprVersion = 1;
constexpr std::size_t kTailTableBytes = 8U * kCprTailImages;

struct RawRecord {
  std::array<char, 4> tag{};
  std::uint32_t name{};
  std::uint32_t qualified_name{};
  std::uint32_t tail{};
  std::uint32_t argument_count{};
  std::uint32_t first_argument{};
  std::uint32_t slot_offset{};
  std::uint32_t slot_size{};
  std::uint32_t region0_offset{};
  std::uint32_t region0_size{};
  std::uint32_t region1_offset{};
  std::uint32_t region1_size{};
};

RawRecord read_raw_record(std::span<const std::byte> file, std::size_t at) noexcept {
  RawRecord r{};
  r.tag = bytes::tag(file, at);
  r.name = bytes::u32(file, at + 0x04);
  r.qualified_name = bytes::u32(file, at + 0x08);
  r.tail = bytes::u32(file, at + 0x0C);
  r.argument_count = bytes::u32(file, at + 0x10);
  r.first_argument = bytes::u32(file, at + 0x14);
  r.slot_offset = bytes::u32(file, at + 0x18);
  r.slot_size = bytes::u32(file, at + 0x1C);
  r.region0_offset = bytes::u32(file, at + 0x20);
  r.region0_size = bytes::u32(file, at + 0x24);
  r.region1_offset = bytes::u32(file, at + 0x28);
  r.region1_size = bytes::u32(file, at + 0x2C);
  return r;
}

enum class Region : std::uint8_t { None, Zero, One };

Region payload_region(RecordTag tag) noexcept {
  switch (tag) {
  case RecordTag::User:
    return Region::Zero;
  case RecordTag::Dds:
  case RecordTag::Ddsc:
  case RecordTag::Dxvb:
  case RecordTag::Dxib:
    return Region::One;
  case RecordTag::Call:
  case RecordTag::Dxps:
  case RecordTag::Dxvs:
  case RecordTag::Unknown:
    return Region::None;
  }
  return Region::None;
}

struct Blocks {
  std::span<const std::byte> arguments{};
  std::span<const std::byte> names{};
  std::span<const std::byte> slots{};
  std::span<const std::byte> region0{};
  std::span<const std::byte> region1{};
};

} // namespace

std::span<const CprArgument> CprContainer::arguments_of(const CprRecord& record) const noexcept {
  if (record.argument_count == 0U) {
    return {};
  }
  return std::span<const CprArgument>(arguments_)
      .subspan(record.first_argument, record.argument_count);
}

const CprRecord* CprContainer::find_record(std::string_view short_name) const noexcept {
  for (const CprRecord& record : records_) {
    if (record.short_name == short_name) {
      return &record;
    }
  }
  return nullptr;
}

const CprRecord*
CprContainer::find_by_qualified_name(std::string_view qualified_name) const noexcept {
  if (qualified_name.empty()) {
    return nullptr;
  }
  for (const CprRecord& record : records_) {
    if (equals_ignoring_ascii_case(record.qualified_name, qualified_name)) {
      return &record;
    }
  }
  return nullptr;
}

std::array<std::span<const std::byte>, kCprTailImages>
CprContainer::tail_images(const CprRecord& record) const noexcept {
  std::array<std::span<const std::byte>, kCprTailImages> images{};
  if (record.tail_offset == kCprNoTail) {
    return images;
  }
  // Validated by parse_cpr: the tables and every image are inside the file.
  const std::size_t tables = record.tail_offset;
  const std::size_t base = tables + kTailTableBytes;
  for (std::size_t i = 0; i < kCprTailImages; ++i) {
    const std::uint32_t offset = bytes::u32(file_, tables + (4U * i));
    const std::uint32_t size = bytes::u32(file_, tables + (4U * kCprTailImages) + (4U * i));
    images[i] = file_.subspan(base + offset, size);
  }
  return images;
}

bool equals_ignoring_ascii_case(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) {
    return false;
  }
  const auto fold = [](char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  };
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (fold(a[i]) != fold(b[i])) {
      return false;
    }
  }
  return true;
}

CprError parse_cpr(std::span<const std::byte> file, CprContainer& container) {
  using E = CprError;
  if (file.size() < kCprHeaderBytes) {
    return E::TooShort;
  }
  if (bytes::tag(file, 0) != kCprMagic || bytes::u32(file, 0x08) != kByteOrderWord) {
    return E::BadMagic;
  }
  CprHeader h{};
  h.version = bytes::u32(file, 0x04);
  if (h.version != kCprVersion) {
    return E::UnsupportedVersion;
  }
  h.record_table_bytes = bytes::u32(file, 0x10);
  h.argument_table_bytes = bytes::u32(file, 0x14);
  h.names_bytes = bytes::u32(file, 0x18);
  h.slot_bytes = bytes::u32(file, 0x1C);
  h.region0_bytes = bytes::u32(file, 0x20);
  h.region1_bytes = bytes::u32(file, 0x24);
  h.page_pool_bytes = bytes::u32(file, 0x34);
  const std::uint64_t tables = static_cast<std::uint64_t>(h.record_table_bytes) +
                               h.argument_table_bytes + h.names_bytes + h.slot_bytes;
  h.region0_base = kCprHeaderBytes + tables;
  h.region1_base = h.region0_base + h.region0_bytes;
  if (tables != bytes::u32(file, 0x2C) || h.record_table_bytes % kCprRecordBytes != 0U ||
      !bytes::within(h.region1_base, h.region1_bytes, file.size())) {
    return E::LayoutInconsistent;
  }
  Blocks b{};
  const std::size_t arguments_at = kCprHeaderBytes + h.record_table_bytes;
  b.arguments = file.subspan(arguments_at, h.argument_table_bytes);
  b.names = file.subspan(arguments_at + h.argument_table_bytes, h.names_bytes);
  b.slots = file.subspan(arguments_at + h.argument_table_bytes + h.names_bytes, h.slot_bytes);
  b.region0 = file.subspan(static_cast<std::size_t>(h.region0_base), h.region0_bytes);
  b.region1 = file.subspan(static_cast<std::size_t>(h.region1_base), h.region1_bytes);

  const std::size_t record_count = h.record_table_bytes / kCprRecordBytes;
  const std::size_t argument_count = h.argument_table_bytes / kCprArgumentBytes;
  std::vector<RawRecord> raw(record_count);
  for (std::size_t i = 0; i < record_count; ++i) {
    raw[i] = read_raw_record(file, kCprHeaderBytes + (i * kCprRecordBytes));
  }

  for (const RawRecord& r : raw) {
    if (r.name >= b.names.size() || r.qualified_name >= b.names.size()) {
      return E::NameOutOfRange;
    }
  }
  std::vector<CprRecord> records(record_count);
  for (std::size_t i = 0; i < record_count; ++i) {
    const bytes::CString name = bytes::c_string(b.names, raw[i].name);
    const bytes::CString qualified = bytes::c_string(b.names, raw[i].qualified_name);
    if (!name.found || !qualified.found) {
      return E::NameUnterminated;
    }
    records[i].tag_bytes = raw[i].tag;
    records[i].tag = record_tag(raw[i].tag);
    records[i].short_name = name.text;
    records[i].qualified_name = raw[i].qualified_name == 0U ? std::string_view{} : qualified.text;
    records[i].tail_offset = raw[i].tail;
    records[i].argument_count = raw[i].argument_count;
    records[i].first_argument = raw[i].first_argument;
  }
  for (std::size_t i = 0; i < record_count; ++i) {
    const RawRecord& r = raw[i];
    switch (payload_region(records[i].tag)) {
    case Region::Zero:
      if (!bytes::within(r.region0_offset, r.region0_size, b.region0.size())) {
        return E::PayloadOutOfRange;
      }
      records[i].payload = b.region0.subspan(r.region0_offset, r.region0_size);
      break;
    case Region::One:
      if (!bytes::within(r.region1_offset, r.region1_size, b.region1.size())) {
        return E::PayloadOutOfRange;
      }
      records[i].payload = b.region1.subspan(r.region1_offset, r.region1_size);
      break;
    case Region::None:
      break;
    }
  }
  for (const RawRecord& r : raw) {
    if (r.argument_count != 0U &&
        !bytes::within(r.first_argument, r.argument_count, argument_count)) {
      return E::ArgumentRangeOutOfRange;
    }
  }
  std::vector<CprArgument> arguments(argument_count);
  for (std::size_t k = 0; k < argument_count; ++k) {
    const std::size_t at = k * kCprArgumentBytes;
    CprArgument& a = arguments[k];
    a.kind = bytes::u32(b.arguments, at + 4);
    a.raw_value = bytes::u32(b.arguments, at + 8);
    const bytes::CString slot = bytes::c_string(b.names, bytes::u32(b.arguments, at));
    if (!slot.found) {
      return E::ArgumentValueOutOfRange;
    }
    a.slot_name = slot.text;
    if (a.kind == kCprArgumentKindName) {
      const bytes::CString value = bytes::c_string(b.names, a.raw_value);
      if (!value.found) {
        return E::ArgumentValueOutOfRange;
      }
      a.value = value.text;
    }
  }
  for (std::size_t i = 0; i < record_count; ++i) {
    if (!bytes::within(raw[i].slot_offset, raw[i].slot_size, b.slots.size())) {
      return E::SlotOutOfRange;
    }
    records[i].slot = b.slots.subspan(raw[i].slot_offset, raw[i].slot_size);
  }
  for (const RawRecord& r : raw) {
    if (r.tail == kCprNoTail) {
      continue;
    }
    if (!bytes::within(r.tail, kTailTableBytes, file.size())) {
      return E::TailOutOfRange;
    }
    const std::uint64_t base = static_cast<std::uint64_t>(r.tail) + kTailTableBytes;
    for (std::size_t i = 0; i < kCprTailImages; ++i) {
      const std::uint64_t offset = bytes::u32(file, r.tail + (4U * i));
      const std::uint64_t size = bytes::u32(file, r.tail + (4U * kCprTailImages) + (4U * i));
      if (!bytes::within(base + offset, size, file.size())) {
        return E::TailOutOfRange;
      }
    }
  }

  container.file_ = file;
  container.header_ = h;
  container.records_ = std::move(records);
  container.arguments_ = std::move(arguments);
  return E::None;
}

} // namespace bw::content_converter
