// Memory: explicit-domain span parsing copies directly into final owned bytes;
// member/name/index containers retain that domain through copies and moves.
// Legacy Bytes parsing is tool/test compatibility; exhaustion never falls back.
// Malformed input and allocation failure unwind partial owners. Lookups
// allocate no storage and return spans borrowing this package. Design R.1
// (#rr-raii), implementation E.6 (#re-raii), both-phase MEM.6
// (guidelines/memory/ MEM.6-inject-allocators-behind-one-interface.md)
// propagate nested ownership. See
// research/engine/subsystems/memory-package-ownership.md for source basis. A1:
// named-file package decoding (DPK v1). parse owns bytes; views borrow until
// destruction/move/assignment. Errors identify malformed/unsupported inputs.
// Exact case-sensitive names; compressed members are identified but not
// decoded. Algorithm: bound header/table/name pool/payloads, index unique
// member names, return checked member slices. Cost O(bytes + names + N log N);
// O(log N) find. Optimization: members share the package owner; member access
// copies no bytes. Design: cpp-guidelines R.20 (#rr-owner) makes archive
// retention explicit; cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) keeps container
// owners rule-of-zero. Implementation: SL.con.3 (#rsl-bounds) bounds every
// decoded slice; COPY.4 avoids accidental owning-container copies.

// Naming design/implementation consultation: cpp-guidelines P.3 (#rp-what)
// selects role-based names. cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) preserves owning values and implicit
// moves; renaming adds no wrapper, allocation or payload copy.

#pragma once
#include <map>

#include "bw/formats/common.hpp"
#include "bw/memory/allocator.hpp"
namespace bw::resources {
struct PackageMember {
  memory::String name;
  Range stored;
  std::uint32_t declared_size;
};
class NamedFilePackage {
 public:
  static NamedFilePackage parse(Bytes bytes);
  static NamedFilePackage parse(std::span<const std::uint8_t> bytes,
                                const std::shared_ptr<memory::Domain>& domain);
  const auto& members() const { return members_; }
  std::span<const std::uint8_t> member(std::string_view name) const;

 private:
  explicit NamedFilePackage(const memory::Allocator<std::uint8_t>& allocator)
      : bytes_(allocator), members_(allocator), index_(allocator) {}
  static NamedFilePackage parse_owned(memory::Vector<std::uint8_t> bytes);
  memory::Vector<std::uint8_t> bytes_;
  memory::Vector<PackageMember> members_;
  std::map<memory::String, std::size_t, std::less<>,
           memory::Allocator<std::pair<const memory::String, std::size_t>>>
      index_;
};
}  // namespace bw::resources
