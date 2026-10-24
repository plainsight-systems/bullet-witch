// Publish the source light's world position from the same body/joint
// association as the prop palette. The source orientation flips do not change
// translation; scalar point-light emission has no direction. Header owns the
// failure and lifetime contracts. Both MCP phases I.1 (ri-explicit), MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) preserve explicit
// borrowed inputs and no allocation. No registry or GPU state is mutated.
#include "placed_lighting.hpp"

#include <cmath>
#include <stdexcept>

namespace bw::game {
std::optional<scene::LocalLight> prepare_placed_light(
    const formats::PhysicsCollisionDefinition& definition,
    std::span<const formats::PlacementMatrix> body_world,
    const formats::PlacementMatrix* object_world, std::size_t joint_count) {
  if (!definition.local_light()) return std::nullopt;
  if (body_world.size() != definition.body_joints().size()) {
    throw std::invalid_argument("Incomplete placed light body palette");
  }
  const auto& source = *definition.local_light();
  const formats::PlacementMatrix* transform = object_world;
  bool found = false;
  if (source.joint >= 0 &&
      static_cast<std::size_t>(source.joint) < joint_count) {
    for (std::size_t body = 0; body < body_world.size(); ++body) {
      if (definition.body_joints().at(body) != source.joint) continue;
      if (found) throw std::invalid_argument("Ambiguous placed light joint");
      transform = &body_world[body];
      found = true;
    }
  }
  if (source.joint >= 0 &&
      static_cast<std::size_t>(source.joint) < joint_count && !found) {
    throw std::invalid_argument(
        "Placed light joint has no body-world publication");
  }
  // Original joint lookup falls back to the owning object's complete transform.
  if (transform == nullptr) {
    throw std::invalid_argument(
        "Placed light needs an explicit owner transform");
  }
  for (const auto& row : *transform) {
    for (const auto value : row) {
      if (!std::isfinite(value)) {
        throw std::invalid_argument("Nonfinite placed light transform");
      }
    }
  }
  return scene::LocalLight{
      {(*transform)[3][0], (*transform)[3][1], (*transform)[3][2]},
      source.diffuse,
      source.specular,
      source.attenuation};
}
}  // namespace bw::game
