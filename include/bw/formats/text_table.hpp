// Original mission text-tree registration data, without pointer relocation.
// The caller owns immutable bytes for the lifetime of this borrowed view;
// parsing validates every directory, link and string before publication. No
// allocation, text rendering, language choice or mutation occurs. Each of the
// five original record tables retains its exact indexes and links; callers can
// traverse the validated tree without interpreting host pointers. Costs
// O(records + referenced string bytes), then O(1) record lookup. Design and
// implementation SL.con.3 (#rsl-bounds) bounds tables/references; E.6
// (#re-raii) keeps registration atomic. Performance MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) requires the owning caller's
// domain for payload acquisition; this view allocates nothing. Evidence:
// mission-load-consumers.md.
#pragma once
#include <array>

#include "bw/formats/common.hpp"
namespace bw::formats {
struct TextLink {
  std::size_t first{}, count{};
};
struct TextNode {
  std::string_view text;
  std::uint32_t identifier{};
  TextLink siblings, children, properties;
};
struct TextProperty {
  std::string_view name, value;
};
class TextTable {
 public:
  static TextTable parse(std::span<const std::uint8_t> bytes);
  std::size_t records(std::size_t table) const;
  TextNode node(std::size_t table, std::size_t index) const;
  TextProperty property(std::size_t index) const;

 private:
  std::string_view text(std::uint32_t offset) const;
  struct EncodedLink {
    std::uint32_t offset{}, count{};
  };
  TextLink link(std::size_t table, EncodedLink encoded) const;
  std::span<const std::uint8_t> bytes_;
  static constexpr std::size_t kTableCount = 6;
  std::array<resources::Range, kTableCount> tables_{};
};
}  // namespace bw::formats
