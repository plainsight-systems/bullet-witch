// A1: named-file package decoding (DPK v1). parse owns bytes; views borrow
// until destruction/move/assignment. Errors identify malformed/unsupported
// inputs. Exact case-sensitive names; compressed members are identified but not
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

#include "bw/formats/named_file_package.hpp"

#include "reader.hpp"
namespace bw::resources {
NamedFilePackage NamedFilePackage::parse(Bytes bytes) {
  constexpr std::uint64_t header_size = 32, stride = 32, fields = 16;
  constexpr std::uint64_t version_field = 8, base_field = 12, count_field = 16;
  NamedFilePackage out;
  out.bytes_ = std::move(bytes);
  detail::Reader r{out.bytes_};
  r.range(0, header_size);
  if (r.tag(0) != std::string("dpk\0", 4)) {
    detail::malformed("Not a DPK");
  }
  if (r.u32(version_field) != 1) {
    detail::unsupported("DPK version");
  }
  const auto table = r.u32(4), base = r.u32(base_field),
             count = r.u32(count_field);
  const auto table_size = static_cast<std::uint64_t>(count) * stride;
  r.range(table, table_size);
  r.range(base, 0);
  if (table < header_size || base < table + table_size) {
    detail::malformed("DPK table/data overlap");
  }
  const Range names{table + table_size, base - (table + table_size)};
  for (std::uint64_t i = 0; i < count; ++i) {
    const auto at = table + i * stride + fields;
    const auto stored = r.u32(at), size = r.u32(at + 4), offset = r.u32(at + 8),
               name = r.u32(at + 12);
    r.range(offset, stored);
    if (offset < base || name < names.offset) {
      detail::malformed("DPK member location");
    }
    PackageMember member{
        r.text(names, static_cast<std::uint32_t>(name - names.offset)),
        {offset, stored},
        size};
    if (member.name.empty() ||
        !out.index_.emplace(member.name, out.members_.size()).second) {
      detail::malformed("Duplicate/empty DPK member name");
    }
    out.members_.push_back(std::move(member));
  }
  return out;
}
std::span<const std::uint8_t> NamedFilePackage::member(
    std::string_view name) const {
  auto found = index_.find(name);
  if (found == index_.end()) {
    throw Error(ErrorCode::not_found, "DPK member: " + std::string(name));
  }
  const auto& entry = members_.at(found->second);
  if (entry.stored.size != entry.declared_size) {
    detail::unsupported("Compressed DPK member");
  }
  return std::span<const std::uint8_t>(bytes_).subspan(
      static_cast<std::size_t>(entry.stored.offset),
      static_cast<std::size_t>(entry.stored.size));
}
}  // namespace bw::resources
