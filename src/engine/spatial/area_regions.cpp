// Recover source first-containing-region selection with owned prepared
// geometry. Header owns bounds/order/lifetime. Implementation: Core CP.3
// (#rconc-data) keeps queries immutable; performance MEM.9
// (embedded-allocate-at-init) moves trig and storage preparation outside
// queries. Both corpora consulted.
#include "bw/spatial/area_regions.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/euler.hpp"
namespace bw::scene {
AreaRegions::AreaRegions(const formats::AreaDefinition& definition,
                         const std::shared_ptr<memory::Domain>& domain)
    : regions_(memory::Allocator<Region>(domain)) {
  constexpr std::uint64_t kRadius = 12, kExtent = 16, kAngles = 32,
                          kTranslation = 48, kIdentifier = 64, kWord = 4;
  regions_.reserve(definition.records().size());
  for (const auto bytes : definition.records()) {
    const resources::detail::Reader reader{bytes};
    Region region;
    math::EulerPlacement<float> placement{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
      region.sphere_center.at(axis) =
          std::bit_cast<float>(reader.u32(axis * kWord));
      region.extent.at(axis) =
          std::bit_cast<float>(reader.u32(kExtent + axis * kWord));
      placement.radians.at(axis) =
          std::bit_cast<float>(reader.u32(kAngles + axis * kWord));
      placement.translation.at(axis) =
          std::bit_cast<float>(reader.u32(kTranslation + axis * kWord));
    }
    region.sphere_radius = std::bit_cast<float>(reader.u32(kRadius));
    region.identifier = std::bit_cast<std::int32_t>(reader.u32(kIdentifier));
    if (!math::all_finite(region.sphere_center) ||
        !math::all_finite(region.extent) ||
        !math::all_finite(placement.radians) ||
        !math::all_finite(placement.translation) ||
        !std::isfinite(region.sphere_radius) || region.sphere_radius < 0 ||
        std::ranges::any_of(
            region.extent, [](float value) { return value < 0; })) {
      throw std::invalid_argument("Invalid authored area geometry");
    }
    region.inverse =
        math::affine_part(math::euler_xyz_transform(placement).inverse);
    if (!math::all_finite(region.inverse)) {
      throw std::invalid_argument("Unrepresentable area transform");
    }
    regions_.push_back(region);
  }
}
std::optional<std::int32_t> AreaRegions::containing_id(
    const math::Vector3<float>& point) const {
  if (!math::all_finite(point)) {
    throw std::invalid_argument("Nonfinite area query");
  }
  for (const auto& region : regions_) {
    const auto delta = math::subtract(point, region.sphere_center);
    if (math::dot(delta, delta) > region.sphere_radius * region.sphere_radius) {
      continue;
    }
    const auto local = math::transform_point_basis_first(point, region.inverse);
    bool inside = true;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      inside = inside && -region.extent.at(axis) < local.at(axis) &&
               local.at(axis) < region.extent.at(axis);
    }
    if (inside) return region.identifier;
  }
  return std::nullopt;
}
}  // namespace bw::scene
