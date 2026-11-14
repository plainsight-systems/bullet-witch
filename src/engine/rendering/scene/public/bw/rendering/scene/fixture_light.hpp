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
//   in world space. The values are photometric, linear Rec. 709: color is
//   the light's illuminance in lux on a surface that faces it, so a
//   diffuse surface of albedo a facing the light sends out a * color / pi
//   cd/m2; ambient is the luminance in cd/m2 of a sky that is the same in
//   every direction: the light a surface of albedo 1 sends out under it,
//   and what a mirror shows of it (forward_pass.hpp, contract 3).
//   fixture_light() is the one value the model scene draws with:
//   direction normalize(0.4, 0.8, 0.45), ambient (8000, 8000, 8000), a
//   clear sky, and color pi times that, (25132.741, 25132.741,
//   25132.741): a key that puts on a white surface facing it the
//   luminance the sky puts on it, so the side the key misses is one stop
//   under the side it lights and still reads. kFixtureEv100 is the
//   exposure of a camera set for this light, by the incident-light rule:
//   the EV100 at which a middle grey surface that faces the light and the
//   sky is shown at middle grey, log2((color / pi + ambient) / 1.2) =
//   log2(16000 / 1.2). A scene that holds nothing but a subject under the
//   fixture pins its exposure there (bw/rendering/model_scene.hpp,
//   model_scene_grade), since a meter with one subject to average exposes
//   for the subject's albedo.
//
// The key is not a measured sun: its illuminance is the sky's luminance
// times pi, so a white surface facing it receives from the key what it
// receives from the sky.
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

// The camera's EV100 under the fixture light (the contract above).
inline constexpr float kFixtureEv100 = 13.7027F;

[[nodiscard]] FixtureLight fixture_light() noexcept;

} // namespace bw::scene

using FixtureLight = bw::scene::FixtureLight;
BW_PARAMETER_BLOCK_LAYOUT(FixtureLight, BW_PARAMETER_MEMBER(FixtureLight, direction),
                          BW_PARAMETER_MEMBER(FixtureLight, color),
                          BW_PARAMETER_MEMBER(FixtureLight, ambient));
#endif
