#include "bw/rendering/scene/fixture_light.hpp"

#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

// Responsibility: the out-of-line part of bw/rendering/scene/fixture_light.hpp.
namespace bw::scene {

// A clear sky, and a key that puts on a white surface facing it the
// luminance the sky does: the key's illuminance in lux is pi times the
// sky's luminance in cd/m2 (fixture_light.hpp).
FixtureLight fixture_light() noexcept {
  math::Vector3f direction{0.4F, 0.8F, 0.45F};
  // The vector is far from zero, so the normalization always takes.
  static_cast<void>(direction.normalize());
  FixtureLight light{};
  light.direction = shaders::Float3{direction.x, direction.y, direction.z};
  light.color = shaders::Float3{25132.741F, 25132.741F, 25132.741F};
  light.ambient = shaders::Float3{8000.0F, 8000.0F, 8000.0F};
  return light;
}

} // namespace bw::scene
