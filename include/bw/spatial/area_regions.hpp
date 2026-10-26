// Prepared source region queries. Construction copies authored AREA sphere,
// extents, inverse Euler placement and signed identifier into the supplied
// domain. Input definitions/bytes may then expire. Queries return the first
// authored region whose inclusive sphere and strict oriented-box interior both
// contain the finite world point; a box-face point is outside. No match returns
// nullopt (the game adapter owns default-row policy). Source482590 applies
// negative translation, -Z, -Y, -X rotations; preparation uses shared math.
// Invalid/nonfinite geometry rejects. O(regions) query, setup-only allocation,
// immutable query data supports independent readers. Source contract in
// lighting-scene-producer-contract.md. Design: CP.3 (#rconc-data) avoids shared
// writable query state; MEM.9 prepares owned records before steady state.
#pragma once
#include <optional>

#include "bw/content/formats/stage_definitions.hpp"
#include "bw/math/matrix.hpp"
namespace bw::scene {
class AreaRegions {
 public:
  AreaRegions(const formats::AreaDefinition& definition,
              const std::shared_ptr<memory::Domain>& domain);
  std::optional<std::int32_t> containing_id(
      const math::Vector3<float>& point) const;

 private:
  struct Region {
    math::Vector3<float> sphere_center{}, extent{};
    float sphere_radius{};
    math::Affine3<float> inverse{};
    std::int32_t identifier{};
  };
  memory::Vector<Region> regions_;
};
}  // namespace bw::scene
