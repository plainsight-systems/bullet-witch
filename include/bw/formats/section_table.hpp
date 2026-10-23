// Bounded original section-table views for placement, cover and score inputs.
// Parsing borrows immutable bytes from the caller through this view's lifetime;
// only section metadata is owned using the injected domain. Copying a view does
// not extend payload ownership. Invalid signatures, ranges, names or indexes
// throw resources::Error. Missing cells remain absent, preserving the native
// distinction from zero-valued cells; typed access interprets original words
// only when the owning schema requests that type. Calls need no synchronization
// while the backing bytes remain immutable. Algorithm: validate STBL header and
// section directory -> bound each row-major cell table -> expose exact-name
// section lookup and bounded typed cells. O(sections squared) duplicate-checked
// setup, O(sections) name lookup, O(1) numeric access, O(string bytes) text
// access; no copied bulk payload. Design E.6 (#re-raii) owns partial metadata
// construction; performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) retains
// the explicit allocation domain. Native consumers and schema are recorded in
// research/engine/subsystems/mission-load-consumers.md.
#pragma once
#include "bw/formats/common.hpp"
#include "bw/memory/allocator.hpp"
namespace bw::formats {
struct TableSection {
  std::string_view name;
  std::uint32_t rows{}, columns{};
  std::uint32_t cells{};
};
struct TableCell {
  std::size_t section{}, row{}, column{};
};
class SectionTable {
 public:
  static SectionTable parse(std::span<const std::uint8_t> bytes,
                            const std::shared_ptr<memory::Domain>& domain);
  const auto& sections() const { return sections_; }
  std::optional<std::size_t> find(std::string_view name) const;
  std::optional<float> number(TableCell cell) const;
  std::optional<std::int32_t> integer(TableCell cell) const;
  std::optional<std::string_view> text(TableCell cell) const;

 private:
  SectionTable(std::span<const std::uint8_t> bytes,
               const std::shared_ptr<memory::Domain>& domain)
      : bytes_(bytes), sections_(memory::Allocator<TableSection>(domain)) {}
  std::optional<std::uint32_t> word(TableCell cell) const;
  std::span<const std::uint8_t> bytes_;
  memory::Vector<TableSection> sections_;
};
}  // namespace bw::formats
