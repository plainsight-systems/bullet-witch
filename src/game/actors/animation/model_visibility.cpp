// Source fallback acquisition/destruction changes shared model bits while each
// selected motion bank retains its own acquired entries. This is deliberately
// not reference-counted across banks: original destruction clears the bit.
#include "model_visibility.hpp"

#include <algorithm>
#include <stdexcept>

namespace bw::game {
ModelVisibility::ModelVisibility(
    std::span<const std::uint32_t, kWords> initial) {
  std::ranges::copy(initial, groups_.begin());
}
void ModelVisibility::command(std::size_t bank, std::int32_t target,
                              bool enabled) {
  auto& active = active_.at(bank);
  if (target < 0) throw std::invalid_argument("Negative visibility identity");
  if (target == 0) return;
  const auto group = static_cast<std::uint32_t>(target);
  const auto word = group / kWordBits;
  if (word >= kWords) return;  // Original bounded model accessors.
  const auto bit = std::uint32_t{1} << (group % kWordBits);
  if (enabled) {
    if ((active.at(word) & bit) == 0) {
      active.at(word) |= bit;
      std::span(groups_)[word] |= bit;
    }
  } else if ((active.at(word) & bit) != 0) {
    active.at(word) &= ~bit;
    std::span(groups_)[word] &= ~bit;
  }
}
void ModelVisibility::reset(std::size_t bank) {
  auto& active = active_.at(bank);
  for (std::size_t word = 0; word < kWords; ++word) {
    std::span(groups_)[word] &= ~active.at(word);
    active.at(word) = 0;
  }
}
bool ModelVisibility::visible(std::uint32_t group) const noexcept {
  const auto word = group / kWordBits;
  return word < kWords && (std::span(groups_)[word] &
                           (std::uint32_t{1} << (group % kWordBits))) != 0;
}
void ModelVisibility::set_group(std::uint32_t group, bool enabled) noexcept {
  const auto word = group / kWordBits;
  if (word >= kWords) return;
  const auto bit = std::uint32_t{1} << (group % kWordBits);
  if (enabled) {
    std::span(groups_)[word] |= bit;
  } else {
    std::span(groups_)[word] &= ~bit;
  }
}
}  // namespace bw::game
