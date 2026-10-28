// Prepare distinct segment-query and physical-contact banks from already
// decoded collision. Source filters remain in StaticCollision registration;
// this owner groups copied faces by bank/flags/material for provider cooking.
// The original material index is separate from each triangle's game surface.
// Original static friction is retained in imported recipes; Jolt exposes one
// Coulomb coefficient, so this adapter selects authored dynamic friction.
// Combine rules preserve the original arithmetic and priority. This is an
// explicit solver adaptation, not exact PhysX contact parity. Both-phase E.6
// (#re-raii) retires partially built worlds, MEM.9 keeps staging at load time.
#include "world_physics.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace bw::game {
namespace {
constexpr std::uint32_t kTriangleReceiver = 4;
struct Face {
  scene::PreparedCollisionGeometry geometry;
  std::uint8_t layer{}, material{};
};
auto key(const Face& face) {
  return std::tuple{face.layer, face.material, face.geometry.object_flags,
                    face.geometry.source};
}
physics::Material material(const ScenePhysicsMaterial& source) {
  if (source.combine_mode > 3) {
    throw std::invalid_argument("Invalid physics material combination");
  }
  const auto combine =
      static_cast<physics::MaterialCombine>(source.combine_mode);
  return {source.dynamic_friction, source.restitution, combine, combine};
}
physics::Configuration configuration_for(physics::Configuration input,
                                         const ScenePhysicsRecipe& recipe) {
  input.gravity = recipe.gravity;
  // Query banks do not participate in contacts, even with other query banks.
  constexpr std::uint16_t query_mask = (1U << character_scenery_layer) |
                                       (1U << environment_scenery_layer) |
                                       (1U << combined_scenery_layer);
  for (auto& mask : input.collision_masks) {
    mask &= static_cast<std::uint16_t>(~query_mask);
  }
  for (const auto layer : {character_scenery_layer, environment_scenery_layer,
                           combined_scenery_layer}) {
    input.collision_masks.at(layer) = 0;
  }
  input.collision_masks.at(physical_terrain_layer) =
      1U << physical_character_layer;
  input.collision_masks.at(physical_character_layer) =
      (1U << physical_terrain_layer) | (1U << physical_character_layer);
  return input;
}
std::uint8_t query_layer(scene::CollisionSourceMode source) {
  switch (source) {
    case scene::CollisionSourceMode::character:
      return character_scenery_layer;
    case scene::CollisionSourceMode::environment:
      return environment_scenery_layer;
    case scene::CollisionSourceMode::combined:
      return combined_scenery_layer;
  }
  throw std::invalid_argument("Unknown scene collision bank");
}
}  // namespace
WorldPhysics::WorldPhysics(const world::WorldClock& clock,
                           const scene::StaticCollision& geometry,
                           const ScenePhysicsRecipe& recipe,
                           physics::Configuration configuration,
                           const std::shared_ptr<memory::Domain>& domain)
    : runtime_(domain),
      world_(runtime_.create_world(configuration_for(configuration, recipe),
                                   clock)),
      receivers_(memory::Allocator<Receiver>(domain)),
      character_material_(material(recipe.materials[1])) {
  const auto count = geometry.geometry_count();
  memory::Vector<Face> faces{memory::Allocator<Face>(domain)};
  if (count > faces.max_size() / 2) {
    throw std::length_error("Scene physics geometry exceeds capacity");
  }
  faces.reserve(count * 2);
  for (std::size_t i = 0; i < count; ++i) {
    const auto value = geometry.geometry(i);
    // Frozen prop shapes have no source receiver/material publication here.
    // Their existing custom-controller contacts remain unchanged. They are
    // admitted through their own source object adapter, not mislabeled terrain.
    if (!value.category || value.capsule) {
      continue;
    }
    if (value.query_geometry) {
      faces.push_back({value, query_layer(value.source), 0});
    }
    if (value.physics_material) {
      if (*value.physics_material >= recipe.materials.size()) {
        throw std::invalid_argument("Unknown terrain physics material");
      }
      faces.push_back({value, physical_terrain_layer, *value.physics_material});
    }
  }
  std::ranges::sort(
      faces, [](const Face& a, const Face& b) { return key(a) < key(b); });
  receivers_.reserve(configuration.limits.bodies);
  memory::Vector<physics::Vector3> vertices{
      memory::Allocator<physics::Vector3>(domain)};
  memory::Vector<physics::Triangle> triangles{
      memory::Allocator<physics::Triangle>(domain)};
  for (std::size_t first = 0; first < faces.size();) {
    std::size_t end = first + 1;
    while (end < faces.size() && key(faces.at(end)) == key(faces.at(first))) {
      ++end;
    }
    const auto size = end - first;
    if (size > std::numeric_limits<std::uint32_t>::max() / 3U) {
      throw std::length_error(
          "Scene physics batch exceeds vertex index capacity");
    }
    vertices.clear();
    triangles.clear();
    vertices.reserve(size * 3);
    triangles.reserve(size);
    for (std::size_t i = first; i < end; ++i) {
      const auto& face = faces.at(i).geometry;
      const auto index = static_cast<std::uint32_t>(vertices.size());
      vertices.insert(vertices.end(), face.vertices.begin(),
                      face.vertices.end());
      triangles.push_back(
          {{index, index + 1, index + 2}, face.surface.value_or(0)});
    }
    const auto& batch = faces.at(first);
    const auto shape =
        world_.create_shape(physics::TriangleMesh{vertices, triangles});
    physics::BodyDescription body;
    body.shape = shape;
    body.layer = batch.layer;
    body.query_flags = batch.geometry.object_flags;
    body.material = material(recipe.materials.at(batch.material));
    const auto id = world_.create_body(body);
    if (batch.layer != physical_terrain_layer) {
      receivers_.push_back({id,
                            {kTriangleReceiver, batch.geometry.object_flags,
                             batch.geometry.source}});
    }
    first = end;
  }
  std::ranges::sort(receivers_, {}, [](const Receiver& receiver) {
    return receiver.body.slot;
  });
}
std::optional<WorldPhysicsReceiver> WorldPhysics::receiver(
    const physics::ReceiverId& receiver) const {
  const auto* rigid = std::get_if<physics::BodyId>(&receiver);
  if (rigid == nullptr) return std::nullopt;
  const auto body = *rigid;
  const auto found = std::ranges::lower_bound(
      receivers_, body.slot, {},
      [](const Receiver& receiver) { return receiver.body.slot; });
  if (found == receivers_.end() || found->body != body) {
    return std::nullopt;
  }
  return found->value;
}
}  // namespace bw::game
