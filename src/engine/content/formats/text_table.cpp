// Validate original text-tree references while preserving immutable offsets.
// Empty links intentionally ignore the native unused offset sentinel. Nonempty
// links must align with and fit their target table. Header contract documents
// borrowed ownership, costs and retrieved bounds/allocation rules.
#include "bw/content/formats/text_table.hpp"

#include "bw/content/formats/binary_reader.hpp"
namespace bw::formats {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
constexpr std::size_t kHeaderBytes = 32, kPoolTable = 5, kPropertyTable = 4;
constexpr std::array<std::uint64_t, 5> kRecordBytes{28, 20, 20, 16, 8};
constexpr std::uint64_t kDirectoryOffset = 8, kWordBytes = 4;
constexpr std::uint64_t kSecondLink = 12, kThirdLink = 20;
}  // namespace
TextTable TextTable::parse(std::span<const std::uint8_t> bytes) {
  const Reader reader{bytes};
  reader.range(0, kHeaderBytes);
  if (reader.tag_view(0) != "STMA" || reader.tag_view(4) != "NAGE") {
    resources::detail::unsupported("Unsupported mission text signature");
  }
  TextTable result;
  result.bytes_ = bytes;
  for (std::size_t table = 0; table <= kPoolTable; ++table) {
    const auto begin = reader.u32(kDirectoryOffset + table * kWordBytes);
    const auto end =
        table == kPoolTable
            ? bytes.size()
            : reader.u32(kDirectoryOffset + (table + 1) * kWordBytes);
    if (begin < kHeaderBytes || end < begin) {
      malformed("Invalid text table order");
    }
    reader.range(begin, end - begin);
    result.tables_.at(table) = {begin, end - begin};
    if (table < kPoolTable && (end - begin) % kRecordBytes.at(table) != 0) {
      malformed("Text record table is misaligned");
    }
  }
  // Construction checks every record, including nodes not reachable from root.
  for (std::size_t table = 0; table < kPropertyTable; ++table) {
    for (std::size_t index = 0; index < result.records(table); ++index) {
      (void)result.node(table, index);
    }
  }
  for (std::size_t index = 0; index < result.records(kPropertyTable); ++index) {
    (void)result.property(index);
  }
  return result;
}
std::size_t TextTable::records(std::size_t table) const {
  if (table >= kPoolTable) malformed("Text record table index exceeds schema");
  return static_cast<std::size_t>(tables_.at(table).size /
                                  kRecordBytes.at(table));
}
std::string_view TextTable::text(std::uint32_t offset) const {
  return Reader{bytes_}.text_view(tables_.at(kPoolTable), offset);
}
TextLink TextTable::link(std::size_t table, EncodedLink encoded) const {
  const auto [offset, count] = encoded;
  if (count == 0) return {};
  const auto stride = kRecordBytes.at(table);
  if (offset % stride != 0) malformed("Text link is not record-aligned");
  const auto first = offset / stride;
  const auto available = records(table);
  if (first > available || count > available - first) {
    malformed("Text link exceeds table");
  }
  return {static_cast<std::size_t>(first), count};
}
TextNode TextTable::node(std::size_t table, std::size_t index) const {
  if (table >= kPropertyTable || index >= records(table)) {
    malformed("Text node index exceeds table");
  }
  const Reader reader{bytes_};
  const auto at = tables_.at(table).offset + index * kRecordBytes.at(table);
  TextNode result;
  if (table == kPropertyTable - 1) {
    result.identifier = reader.u32(at);
    result.text = text(reader.u32(at + kWordBytes));
    result.properties = link(kPropertyTable, {reader.u32(at + kSecondLink),
                                              reader.u32(at + 2 * kWordBytes)});
    return result;
  }
  result.text = text(reader.u32(at));
  if (table == 0) {
    result.siblings =
        link(0, {reader.u32(at + 2 * kWordBytes), reader.u32(at + kWordBytes)});
    result.children = link(1, {reader.u32(at + kSecondLink + kWordBytes),
                               reader.u32(at + kSecondLink)});
    result.properties =
        link(kPropertyTable, {reader.u32(at + kThirdLink + kWordBytes),
                              reader.u32(at + kThirdLink)});
  } else {
    result.children = link(table + 1, {reader.u32(at + 2 * kWordBytes),
                                       reader.u32(at + kWordBytes)});
    result.properties =
        link(kPropertyTable, {reader.u32(at + kSecondLink + kWordBytes),
                              reader.u32(at + kSecondLink)});
  }
  return result;
}
TextProperty TextTable::property(std::size_t index) const {
  if (index >= records(kPropertyTable)) {
    malformed("Text property index exceeds table");
  }
  const Reader reader{bytes_};
  const auto at = tables_.at(kPropertyTable).offset +
                  index * kRecordBytes.at(kPropertyTable);
  return {text(reader.u32(at)), text(reader.u32(at + kWordBytes))};
}
}  // namespace bw::formats
