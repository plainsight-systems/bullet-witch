// Inline DDS PNG replacements derive selected size from bounded PNG chunks;
// raw and first/second section sizes remain original metadata. CRC/image
// validation belongs to the image decoder. SL.con.3 (#rsl-bounds) bounds the
// traversal; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// requires no allocation beyond existing parse owners. Both phases consulted.
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

#pragma once
#include "bw/formats/common.hpp"
namespace bw::resources {
struct Argument {
  std::array<std::uint32_t, 3> raw{};
  std::optional<std::string> name;
  std::optional<std::string> string_value;
};
struct ResourceArchiveRecord {
  std::string type, name;
  std::optional<std::string> publication_name;
  std::array<std::uint32_t, kArchiveRecordWordCount> raw{};
  std::vector<Argument> arguments;
  std::optional<Range> first_payload, second_payload, selected_payload;
  bool known_type{};  // Identified native registry entry; no handler is
                      // executed.
};
class ResourceArchive {
 public:
  static ResourceArchive parse(Bytes bytes);
  const auto& header() const { return header_; }
  const auto& sections() const {
    return sections_;
  }  // table, arguments, names, gap, first, second
  const auto& records() const { return records_; }
  const auto& argument_table() const { return arguments_; }
  std::span<const std::uint8_t> bytes() const { return bytes_; }
  std::span<const std::uint8_t> payload(std::size_t record_index) const;

 private:
  Bytes bytes_;
  std::array<std::uint32_t, kArchiveHeaderWordCount> header_{};
  std::array<Range, kSectionCount> sections_{};
  std::vector<ResourceArchiveRecord> records_;
  std::vector<Argument> arguments_;
};
}  // namespace bw::resources
