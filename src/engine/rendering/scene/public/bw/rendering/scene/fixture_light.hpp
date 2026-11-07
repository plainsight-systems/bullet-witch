#ifndef BW_RENDERING_SCENE_FIXTURE_LIGHT_HPP
#define BW_RENDERING_SCENE_FIXTURE_LIGHT_HPP

#include "bw/rendering/shaders/parameter_block.hpp"

#include <cstdint>

// Responsibility: the one light a lit draw has until the scene renderer
// brings light lists: one directional light and one constant ambient
// color, in a static block of their own. It is a fixture, named for what
// it is: it leaves with the first light list, and nothing durable depends
// on its block. The lit shading model that consumes it is durable and is
// stated in forward_pass.hpp.
//
// Contract.
//   FixtureLight is static slot kFixtureLightSlot of the engine root
//   layout. direction is the unit vector from a surface toward the light,
//   in world space; color is the light's radiance scale, linear RGB;
//   ambient is the ambient color, linear RGB. fixture_light() is the one
//   value this slice draws with: direction normalize(0.4, 0.8, 0.45),
//   color (3, 3, 3), ambient (0.3, 0.3, 0.3).
//
// Read guidance: I.1 (the light is data in a block, not a constant in a
// shader).
namespace bw::scene {

// Offsets: direction 0, color 16, ambient 32; size 48.
struct alignas(shaders::kBlockAlignment) FixtureLight {
  shaders::Float3 direction{};
  shaders::Float3 color{};
  shaders::Float3 ambient{};
};
inline constexpr std::uint32_t kFixtureLightBytes = 48;
static_assert(sizeof(FixtureLight) == kFixtureLightBytes);
inline constexpr std::uint32_t kFixtureLightSlot = 1;

[[nodiscard]] FixtureLight fixture_light() noexcept;

} // namespace bw::scene

using FixtureLight = bw::scene::FixtureLight;
BW_PARAMETER_BLOCK_LAYOUT(FixtureLight, BW_PARAMETER_MEMBER(FixtureLight, direction),
                          BW_PARAMETER_MEMBER(FixtureLight, color),
                          BW_PARAMETER_MEMBER(FixtureLight, ambient));
#endif
