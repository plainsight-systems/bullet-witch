// Prepared world-space collision inputs independent of asset formats or a
// rigid-body provider. Triangle winding supplies the outward normal; capsules
// carry hemisphere centers (identical centers encode a sphere). The owner
// validates finite geometry and copies it at registration, then prepares its
// immutable hierarchy. No borrowed asset lifetime crosses this interface.
// Both MCP phases: SL.con.3 (#rsl-bounds) uses fixed coordinate extents; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps allocation in
// setup.
#pragma once
#include <array>
#include <cstdint>
namespace bw::scene {
inline constexpr std::uint32_t kOrdinaryCollisionFlags = 0x10001;
struct CollisionTriangle {
  std::array<std::array<float, 3>, 3> vertices{};
  std::uint32_t object_flags{kOrdinaryCollisionFlags};
};
struct CollisionCapsule {
  std::array<float, 3> start{}, end{};
  float radius{};
  std::uint32_t object_flags{kOrdinaryCollisionFlags};
};
}  // namespace bw::scene
