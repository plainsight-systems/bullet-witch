// Resolve the original receiver reflection slot without renderer ownership.
// Header states table lifetime and fallback policy. Both-phase I.1 names the
// explicit black result; CDSA.32 copies only the two consumed table columns
// once, retaining source row order and independent immutable receiver queries.
#include "bw/lighting/reflection_field.hpp"

#include <algorithm>
#include <stdexcept>

namespace bw::scene {
ReflectionField::ReflectionField(AreaRegions regions,
                                 const formats::SectionTable& table,
                                 ReflectionSlotResidency resident_slots,
                                 const std::shared_ptr<memory::Domain>& domain)
    : regions_(std::move(regions)),
      bindings_(memory::Allocator<Binding>(domain)),
      resident_slots_(resident_slots) {
  if (table.sections().empty() || table.sections().front().rows == 0) {
    throw std::invalid_argument("Reflection table requires a default row");
  }
  const auto& section = table.sections().front();
  constexpr std::size_t kSlotColumn = 2;
  bindings_.reserve(section.rows);
  for (std::size_t row = 0; row < section.rows; ++row) {
    const auto region =
        section.columns > 0 ? table.integer({0, row, 0}).value_or(0) : 0;
    const auto slot = section.columns > kSlotColumn
                          ? table.integer({0, row, kSlotColumn}).value_or(0)
                          : 0;
    bindings_.push_back({region, slot});
  }
}
std::optional<std::uint32_t> ReflectionField::slot_at(
    const math::Vector3<float>& receiver_position) const {
  if (!math::all_finite(receiver_position)) {
    throw std::invalid_argument("Nonfinite reflection receiver position");
  }
  // The original context requires slot zero even when a different selected
  // slot exists. A sparse bank beginning at slot four remains opaque black.
  if (!resident_slots_[0]) {
    return std::nullopt;
  }
  const auto region = std::max<std::int32_t>(
      0, regions_.containing_id(receiver_position).value_or(0));
  const auto found = std::ranges::find(bindings_, region, &Binding::region);
  const auto slot =
      (found == bindings_.end() ? bindings_.front() : *found).slot;
  if (slot < 0 || static_cast<std::uint32_t>(slot) >= kReflectionSlotCount ||
      !resident_slots_.at(static_cast<std::size_t>(slot))) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(slot);
}
}  // namespace bw::scene
