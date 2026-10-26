// Immutable receiver-position reflection selection from the original region
// query and first section table. Construction takes owned prepared regions,
// copies region/slot bindings and residency into the supplied scene domain;
// table bytes may then expire. A configured table requires section zero and
// at least one row. Missing column/cell values retain source integer zero.
// Query normalizes missing/negative region identifiers to zero, finds the first
// matching row or uses the first row, then validates its slot. Mandatory slot
// zero residency gates every lookup; absent/out-of-range selected slots return
// nullopt, meaning original opaque black. No camera state or native handles.
// Queries require finite positions, allocate nothing and are concurrent-safe.
// Setup O(rows), query O(regions + rows); owned fixed residency plus row data.
// Both design/implementation Core I.1 (#ri-explicit) exposes fallback meaning;
// performance CDSA.32 transforms immutable table cells once for this consumer.
#pragma once
#include "bw/content/formats/section_table.hpp"
#include "bw/spatial/area_regions.hpp"

namespace bw::scene {
inline constexpr std::size_t kReflectionSlotCount = 16;
using ReflectionSlotResidency = std::array<bool, kReflectionSlotCount>;
class ReflectionField {
 public:
  ReflectionField(AreaRegions regions, const formats::SectionTable& table,
                  ReflectionSlotResidency resident_slots,
                  const std::shared_ptr<memory::Domain>& domain);
  std::optional<std::uint32_t> slot_at(
      const math::Vector3<float>& receiver_position) const;

 private:
  struct Binding {
    std::int32_t region{}, slot{};
  };
  AreaRegions regions_;
  memory::Vector<Binding> bindings_;
  ReflectionSlotResidency resident_slots_{};
};
}  // namespace bw::scene
