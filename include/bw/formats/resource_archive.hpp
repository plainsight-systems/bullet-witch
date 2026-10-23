// Memory adoption: payload/metadata and nested strings retain an owning domain
// allocator; legacy parser defaults remain compatibility allocations. Copies
// preserve allocator ownership and leases may outlive their service. Allocation
// failures unwind without fallback. R.20 (#rr-owner), F.27 (#rf-shared_ptr),
// E.17 (#re-not-always) govern ownership/unwinding; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) routes
// actual storage; MEM.8
// (guidelines/memory/MEM.8-pmr-allocator-aware-containers.md) motivates
// retaining the resource instead of borrowing a PMR pointer. Both design and
// implementation consulted. See resource-memory-migration.md. Inline DDS PNG
// replacements derive selected size from bounded PNG chunks; raw and
// first/second section sizes remain original metadata. CRC/image validation
// belongs to the image decoder. SL.con.3 (#rsl-bounds) bounds the traversal;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) requires no
// allocation beyond existing parse owners. Both phases consulted. A1: resource
// archive decoding (CPR1) (docs/resources.md). parse owns bytes and throws
// Error for malformed/unsupported input. Metadata/spans borrow until owner
// mutation, move or destruction; copies are independent. No I/O; caller
// synchronizes. Algorithm: validate sections, decode arguments/records, return
// bounded views. Cost: O(bytes + decoded metadata); allocator-aware payload
// moves are O(1), legacy byte conversion copies O(bytes). Optimization:
// native payload ownership transfers without copying. Design: cpp-guidelines
// I.1 (#ri-explicit), C.122
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
#include "bw/memory/allocator.hpp"
namespace bw::resources {
using ArchiveBytes = std::vector<std::uint8_t, memory::Allocator<std::uint8_t>>;
using ArchiveString =
    std::basic_string<char, std::char_traits<char>, memory::Allocator<char>>;
template <class T>
using ArchiveVector = std::vector<T, memory::Allocator<T>>;
struct Argument {
  explicit Argument(memory::Allocator<char> allocator = {})
      : allocator(std::move(allocator)) {}
  memory::Allocator<char> allocator;
  std::array<std::uint32_t, 3> raw{};
  std::optional<ArchiveString> name;
  std::optional<ArchiveString> string_value;
};
struct ResourceArchiveRecord {
  explicit ResourceArchiveRecord(const memory::Allocator<char>& allocator = {})
      : type(allocator), name(allocator), arguments(allocator) {}
  ArchiveString type, name;
  std::optional<ArchiveString> publication_name;
  std::array<std::uint32_t, kArchiveRecordWordCount> raw{};
  ArchiveVector<Argument> arguments;
  std::optional<Range> first_payload, second_payload, selected_payload;
  bool known_type{};  // Identified native registry entry; no handler is
                      // executed.
};
class ResourceArchive {
 public:
  // Compatibility entry: caller's process-heap bytes are copied into legacy
  // archive storage. Native services use the allocator-aware overload below.
  static ResourceArchive parse(Bytes bytes);
  static ResourceArchive parse(ArchiveBytes bytes);
  const auto& header() const { return header_; }
  const auto& sections() const {
    return sections_;
  }  // table, arguments, names, gap, first, second
  const auto& records() const { return records_; }
  const auto& argument_table() const { return arguments_; }
  std::span<const std::uint8_t> bytes() const { return bytes_; }
  std::span<const std::uint8_t> payload(std::size_t record_index) const;

 private:
  explicit ResourceArchive(const memory::Allocator<std::uint8_t>& allocator)
      : bytes_(allocator), records_(allocator), arguments_(allocator) {}
  ArchiveBytes bytes_;
  std::array<std::uint32_t, kArchiveHeaderWordCount> header_{};
  std::array<Range, kSectionCount> sections_{};
  ArchiveVector<ResourceArchiveRecord> records_;
  ArchiveVector<Argument> arguments_;
};
}  // namespace bw::resources
