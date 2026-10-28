// Original mission placement definitions, separate from live actor authority.
// Decode immutable purchased STBL bytes into bounded shapes/archetypes/groups/
// triggers; names and unconsumed scalar cells borrow those bytes through this
// object's lifetime. The caller retains bytes unchanged and the injected domain
// owns metadata. Invalid references/nonfinite geometry fail; unsupported shape
// or distribution forms fail explicitly. No actor is created by decoding.
// Algorithm: bound shape directory -> decode section schemas -> resolve names
// and archetype IDs -> publish const spans. Setup is O(records squared) for
// bounded exact-name joins; lookup/containment allocate nothing. Calls require
// immutable backing, no synchronization. Design Core I.1 (#ri-explicit) states
// borrowed lifetime, C.9 (#rc-private) hides mutable metadata; performance
// MEM.6 (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md)
// injects domain storage, MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps trigger-time
// access allocation-free. See mission-startup-source-trace.md.
#pragma once
#include "bw/content/formats/section_table.hpp"
#include "bw/math/vector.hpp"
namespace bw::content {
enum class PlacementShapeKind : std::uint8_t { box, sphere, cylinder, point };
struct PlacementShape {
  std::string_view name;
  math::Vector3<float> center{}, half_extent{};
  float yaw_radians{};
  PlacementShapeKind kind{PlacementShapeKind::box};
  bool three_dimensional{};
};
struct PlacementArchetype {
  std::int32_t id{};
  std::size_t source_row{};
  std::string_view model, weapon, behavior, path, customization,
      secondary_customization;
  std::int32_t appearance{}, life{}, actor_flags{};
  // Source chara column 27 becomes the queued death-source magic amount.
  // Design I.1/E.4 and MEM.9: validated copied value, no live actor authority.
  std::int32_t defeat_magic{};
  float retirement_distance{};
  bool customization_present{};
};
struct PlacementGroup {
  std::int32_t id{};
  std::size_t source_row{}, shape{}, archetype{};
  std::uint32_t count{};
  float yaw_degrees{}, admission_distance{};
  std::string_view path;
  std::int32_t option{};
};
struct PlacementTrigger {
  std::int32_t id{};
  std::size_t shape{}, source_row{};
};
class MissionPlacementDefinition {
 public:
  static MissionPlacementDefinition decode(
      std::span<const std::uint8_t> bytes,
      const std::shared_ptr<memory::Domain>& domain);
  std::span<const PlacementShape> shapes() const { return shapes_; }
  std::span<const PlacementArchetype> archetypes() const { return archetypes_; }
  std::span<const PlacementGroup> groups() const { return groups_; }
  std::span<const PlacementTrigger> triggers() const { return triggers_; }
  const formats::SectionTable& original_table() const { return table_; }
  // Native 2D trap query uses inclusive boundaries, discards Y, and excludes
  // 3D shapes. This is original placement policy, not generic spatial math.
  bool contains_2d(std::size_t shape,
                   const math::Vector3<float>& position) const;

 private:
  MissionPlacementDefinition(formats::SectionTable table,
                             const std::shared_ptr<memory::Domain>& domain);
  formats::SectionTable table_;
  memory::Vector<PlacementShape> shapes_;
  memory::Vector<PlacementArchetype> archetypes_;
  memory::Vector<PlacementGroup> groups_;
  memory::Vector<PlacementTrigger> triggers_;
};
}  // namespace bw::content
