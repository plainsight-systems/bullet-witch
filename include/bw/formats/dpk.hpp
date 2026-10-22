// A1: bounded DPK v1 member decoding. parse owns bytes; views borrow until
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

#pragma once
#include <map>

#include "bw/formats/common.hpp"
namespace bw::resources {
struct DpkMember {
  std::string name;
  Range stored;
  std::uint32_t declared_size;
};
class DpkPackage {
 public:
  static DpkPackage parse(Bytes bytes);
  const auto& members() const { return members_; }
  std::span<const std::uint8_t> member(std::string_view name) const;

 private:
  Bytes bytes_;
  std::vector<DpkMember> members_;
  std::map<std::string, std::size_t, std::less<>> index_;
};
}  // namespace bw::resources
