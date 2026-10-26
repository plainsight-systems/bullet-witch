// Decode bounded original section metadata and interpret typed cells on demand.
// Immutable backing remains caller-owned, as documented in section_table.hpp.
// Validate directory/cell extents before publishing views; preserve absent
// cells and original integer/float bits. No pointer relocation or copied bulk
// data. Implementation SL.con.3 (#rsl-bounds) bounds every table/name/cell; E.6
// (#re-raii) owns partial metadata. Both-phase performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) passes
// the supplied domain into owned metadata. Costs are documented in the header.
#include "bw/content/formats/section_table.hpp"

#include <bit>

#include "bw/content/formats/binary_reader.hpp"
namespace bw::formats {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
constexpr std::size_t kHeaderBytes = 40;
constexpr std::size_t kSectionBytes = 64;
constexpr std::size_t kNameBytes = 32;
constexpr std::uint64_t kCellBytes = 8;
constexpr std::uint64_t kSectionCount = 0x1c;
constexpr std::uint64_t kSectionOffset = 0x20;
constexpr std::uint64_t kRows = 0x20;
constexpr std::uint64_t kColumns = 0x24;
constexpr std::uint64_t kCells = 0x28;
}  // namespace
SectionTable SectionTable::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  Reader reader{bytes};
  reader.range(0, kHeaderBytes);
  if (reader.tag_view(0) != "STBL" || reader.u32(4) != 1) {
    resources::detail::unsupported(
        "Unsupported section table signature/version");
  }
  const auto count = reader.u32(kSectionCount);
  const auto offset = reader.u32(kSectionOffset);
  reader.range(offset, static_cast<std::uint64_t>(count) * kSectionBytes);
  SectionTable result(bytes, domain);
  result.sections_.reserve(count);
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto position =
        offset + static_cast<std::uint64_t>(index) * kSectionBytes;
    const auto name = reader.text_view({position, kNameBytes}, 0);
    if (result.find(name)) {
      malformed("Duplicate section table name");
    }
    TableSection section{name, reader.u32(position + kRows),
                         reader.u32(position + kColumns),
                         reader.u32(position + kCells)};
    // Divide the remaining byte capacity before multiplying untrusted
    // dimensions.
    reader.range(section.cells, 0);
    const auto capacity = (bytes.size() - section.cells) / kCellBytes;
    if (section.rows != 0 && section.columns > capacity / section.rows) {
      malformed("Section table cells exceed retained bytes");
    }
    result.sections_.push_back(section);
  }
  return result;
}
std::optional<std::size_t> SectionTable::find(std::string_view name) const {
  for (std::size_t index = 0; index < sections_.size(); ++index) {
    if (sections_.at(index).name == name) return index;
  }
  return std::nullopt;
}
std::optional<std::uint32_t> SectionTable::word(TableCell cell) const {
  if (cell.section >= sections_.size()) {
    malformed("Section table section index is out of range");
  }
  const auto& section = sections_.at(cell.section);
  if (cell.row >= section.rows || cell.column >= section.columns) {
    malformed("Section table cell index is out of range");
  }
  const auto offset =
      section.cells +
      (static_cast<std::uint64_t>(cell.row) * section.columns + cell.column) *
          kCellBytes;
  const Reader reader{bytes_};
  if (reader.u32(offset) == 0) return std::nullopt;
  return reader.u32(offset + 4);
}
std::optional<float> SectionTable::number(TableCell cell) const {
  const auto value = word(cell);
  return value ? std::optional<float>(std::bit_cast<float>(*value))
               : std::nullopt;
}
std::optional<std::int32_t> SectionTable::integer(TableCell cell) const {
  const auto value = word(cell);
  return value
             ? std::optional<std::int32_t>(std::bit_cast<std::int32_t>(*value))
             : std::nullopt;
}
std::optional<std::string_view> SectionTable::text(TableCell cell) const {
  const auto offset = word(cell);
  if (!offset || *offset == 0) return std::nullopt;
  const Reader reader{bytes_};
  reader.range(*offset, 1);
  return reader.text_view({*offset, bytes_.size() - *offset}, 0);
}
}  // namespace bw::formats
