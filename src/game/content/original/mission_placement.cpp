// Math boundary: both-phase Core F.4 / performance MEM.9 keep generic
// fixed-value operations in bw::math; source policy remains with this owner.
// Parse original shape and placement schemas once, preserving row identities.
// Borrowed strings retain caller-owned STBL bytes; metadata uses its injected
// domain. Bounds/finite checks precede publication and partial owners unwind on
// failure. No source bytes are patched. Algorithm/cost and borrowed lifetime
// are in mission_placement.hpp. Implementation Core SL.con.3 (#rsl-bounds)
// checks every indexed field; both-phase performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) owns
// metadata and MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// confines allocations to decode. Source: mission-startup-source-trace.md.
#include "mission_placement.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <utility>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/euler.hpp"
namespace bw::content {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
using resources::detail::unsupported;
constexpr std::size_t kShapeBytes = 96, kShapeName = 32, kShapeFlags = 64,
                      kShapeExtent = 16, kShapeYaw = 28, kArchetypePath = 5,
                      kArchetypeFlags = 6, kArchetypeWeapon = 14,
                      kArchetypeDrawGroups = 15, kArchetypePalette = 25,
                      kArchetypeRetirement = 28, kGroupMode = 5, kGroupYaw = 6,
                      kGroupPath = 7, kGroupOption = 8;
constexpr std::uint32_t kMaximumCount = 256, kKnownShapeFlags = 0x13,
                        kShape3D = 16, kArchetypeColumns = 31,
                        kGroupColumns = 9;
constexpr float kDefaultAdmissionDistance = 100;
float finite(float value) {
  if (!std::isfinite(value)) {
    malformed("Nonfinite mission placement field");
  }
  return value;
}
std::size_t shape_index(std::span<const PlacementShape> shapes,
                        std::string_view name) {
  const auto found = std::ranges::find(shapes, name, &PlacementShape::name);
  if (found == shapes.end()) {
    malformed("Missing mission placement shape");
  }
  return static_cast<std::size_t>(found - shapes.begin());
}
std::string_view text(const formats::SectionTable& table, std::size_t section,
                      std::size_t row, std::size_t column) {
  return table.text({section, row, column}).value_or(std::string_view{});
}
std::int32_t integer(const formats::SectionTable& table, std::size_t section,
                     std::size_t row, std::size_t column) {
  return table.integer({section, row, column}).value_or(0);
}
float number(const formats::SectionTable& table, std::size_t section,
             std::size_t row, std::size_t column) {
  return finite(table.number({section, row, column}).value_or(0));
}
void columns(const formats::SectionTable& table, std::size_t section,
             std::uint32_t required) {
  if (table.sections().at(section).columns < required) {
    malformed("Mission placement section has insufficient columns");
  }
}
std::uint32_t count(std::string_view value) {
  std::uint32_t result{};
  const auto parsed = std::from_chars(std::to_address(value.begin()),
                                      std::to_address(value.end()), result);
  if (parsed.ec != std::errc{} || parsed.ptr != std::to_address(value.end()) ||
      result == 0 || result > kMaximumCount) {
    unsupported("Unsupported mission placement count");
  }
  return result;
}
}  // namespace
MissionPlacementDefinition::MissionPlacementDefinition(
    formats::SectionTable table, const std::shared_ptr<memory::Domain>& domain)
    : table_(std::move(table)),
      shapes_(memory::Allocator<PlacementShape>(domain)),
      archetypes_(memory::Allocator<PlacementArchetype>(domain)),
      groups_(memory::Allocator<PlacementGroup>(domain)),
      triggers_(memory::Allocator<PlacementTrigger>(domain)) {}
MissionPlacementDefinition MissionPlacementDefinition::decode(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  MissionPlacementDefinition result(formats::SectionTable::parse(bytes, domain),
                                    domain);
  const Reader reader{bytes};
  const auto shape_count = reader.u32(12);
  const auto shape_offset = reader.u32(16);
  reader.range(shape_offset,
               static_cast<std::uint64_t>(shape_count) * kShapeBytes);
  result.shapes_.reserve(shape_count);
  // The shape directory is distinct from STBL's row/cell section directory.
  for (std::uint32_t index = 0; index < shape_count; ++index) {
    const auto offset =
        shape_offset + static_cast<std::uint64_t>(index) * kShapeBytes;
    PlacementShape shape;
    shape.name = reader.text_view({offset + kShapeName, kShapeName}, 0);
    if (shape.name.empty() ||
        std::ranges::any_of(result.shapes_, [&](const auto& other) {
          return other.name == shape.name;
        })) {
      malformed("Empty or duplicate mission placement shape");
    }
    for (std::size_t component = 0; component < 3; ++component) {
      shape.center.at(component) =
          finite(std::bit_cast<float>(reader.u32(offset + component * 4)));
      shape.half_extent.at(component) = finite(std::bit_cast<float>(
          reader.u32(offset + kShapeExtent + component * 4)));
      if (shape.half_extent.at(component) < 0) {
        malformed("Negative placement extent");
      }
    }
    shape.yaw_radians =
        finite(std::bit_cast<float>(reader.u32(offset + kShapeYaw)));
    const auto flags = reader.u32(offset + kShapeFlags);
    if ((flags & ~kKnownShapeFlags) != 0 || (flags & 3U) == 3U) {
      unsupported("Unsupported mission placement shape flags");
    }
    shape.kind = static_cast<PlacementShapeKind>(flags & 3U);
    shape.three_dimensional = (flags & kShape3D) != 0;
    result.shapes_.push_back(shape);
  }
  const auto& table = result.table_;
  if (const auto section = table.find("chara")) {
    columns(table, *section, kArchetypeColumns);
    result.archetypes_.reserve(table.sections().at(*section).rows);
    for (std::size_t row = 0; row < table.sections().at(*section).rows; ++row) {
      PlacementArchetype actor;
      actor.id = integer(table, *section, row, 0);
      actor.source_row = row;
      actor.model = text(table, *section, row, 1);
      actor.appearance = integer(table, *section, row, 2);
      actor.life = integer(table, *section, row, 3);
      actor.behavior = text(table, *section, row, 4);
      actor.path = text(table, *section, row, kArchetypePath);
      actor.actor_flags = integer(table, *section, row, kArchetypeFlags);
      actor.weapon = text(table, *section, row, kArchetypeWeapon);
      actor.customization = text(table, *section, row, kArchetypeDrawGroups);
      actor.customization_present =
          table.text({*section, row, kArchetypeDrawGroups}).has_value();
      actor.secondary_customization =
          text(table, *section, row, kArchetypePalette);
      actor.retirement_distance =
          number(table, *section, row, kArchetypeRetirement);
      if (actor.model.empty() ||
          std::ranges::any_of(result.archetypes_, [&](const auto& other) {
            return other.id == actor.id;
          })) {
        malformed("Empty or duplicate mission archetype");
      }
      result.archetypes_.push_back(actor);
    }
  }
  if (const auto section = table.find("group")) {
    columns(table, *section, kGroupColumns);
    result.groups_.reserve(table.sections().at(*section).rows);
    for (std::size_t row = 0; row < table.sections().at(*section).rows; ++row) {
      if (integer(table, *section, row, kGroupMode) != 0) {
        unsupported("Unsupported mission placement distribution mode");
      }
      PlacementGroup group;
      group.id = integer(table, *section, row, 0);
      group.source_row = row;
      group.shape = shape_index(result.shapes_, text(table, *section, row, 1));
      const auto id = integer(table, *section, row, 3);
      const auto found =
          std::ranges::find(result.archetypes_, id, &PlacementArchetype::id);
      if (found == result.archetypes_.end()) {
        malformed("Missing placement archetype");
      }
      group.archetype =
          static_cast<std::size_t>(found - result.archetypes_.begin());
      group.count = count(text(table, *section, row, 4));
      group.yaw_degrees = number(table, *section, row, kGroupYaw);
      group.admission_distance = number(table, *section, row, 2);
      if (group.admission_distance <= 0) {
        group.admission_distance = kDefaultAdmissionDistance;
      }
      group.path = text(table, *section, row, kGroupPath);
      group.option = integer(table, *section, row, kGroupOption);
      if (result.shapes_.at(group.shape).three_dimensional &&
          group.count != 1) {
        unsupported("Three-dimensional group placement is not reconstructed");
      }
      result.groups_.push_back(group);
    }
  }
  if (const auto section = table.find("trap")) {
    columns(table, *section, 2);
    result.triggers_.reserve(table.sections().at(*section).rows);
    for (std::size_t row = 0; row < table.sections().at(*section).rows; ++row) {
      result.triggers_.push_back(
          {integer(table, *section, row, 0),
           shape_index(result.shapes_, text(table, *section, row, 1)), row});
    }
  }
  return result;
}
bool MissionPlacementDefinition::contains_2d(
    std::size_t shape, const math::Vector3<float>& position) const {
  if (!math::all_finite(position)) {
    malformed("Nonfinite placement query");
  }
  const auto& volume = shapes_.at(shape);
  if (volume.three_dimensional) {
    return false;
  }
  const auto delta = math::subtract(position, volume.center);
  if (volume.kind == PlacementShapeKind::box) {
    const auto [x, z] =
        math::rotate_planar(math::Vector<float, 2>{delta[0], delta[2]},
                            math::planar_rotation(volume.yaw_radians));
    return -volume.half_extent[0] <= x && x <= volume.half_extent[0] &&
           -volume.half_extent[2] <= z && z <= volume.half_extent[2];
  }
  return delta[0] * delta[0] + delta[2] * delta[2] <=
         volume.half_extent[0] * volume.half_extent[0];
}
}  // namespace bw::content
