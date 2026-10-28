// Mission-owned provider composition. Prepare source collision banks and the
// separately admitted physical terrain before actors. Copy decoded immutable
// geometry from StaticCollision; never use rendered triangles. Scene settings
// come from purchased native tables. Query-only banks have no solver contacts;
// physical bodies use separate layers. Actor admission retains its own policy.
// Runtime and world retire together; read/mutation/resource capabilities do not
// retain them. Only this owner receives world frames. Calls are serial on the
// world thread. Invalid geometry/material/capacity throws with complete owner
// rollback. Metadata observations copy values and allocate nothing.
// Preparation sorts O(faces log faces), cooks bounded batches, then releases
// CPU staging. Retained metadata is O(batches), queries O(log batches).
// Both phases Core E.6 (#re-raii) orders provider/resource lifetime; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) confines staging to
// setup. Source and provider adaptations: first-combat-physics-boundary.md.
#pragma once
#include "bw/physics/world.hpp"
#include "bw/spatial/static_collision.hpp"
#include "game/content/original/character_physics_recipe.hpp"

namespace bw::game {
inline constexpr std::uint8_t character_scenery_layer = 0;
inline constexpr std::uint8_t environment_scenery_layer = 1;
inline constexpr std::uint8_t combined_scenery_layer = 2;
inline constexpr std::uint8_t physical_terrain_layer = 3;
inline constexpr std::uint8_t physical_character_layer = 4;
struct WorldPhysicsReceiver {
  std::uint32_t kind{}, object_flags{};
  scene::CollisionSourceMode source_bank{};
};
class WorldPhysics {
 public:
  WorldPhysics(const world::WorldClock& clock,
               const scene::StaticCollision& geometry,
               const ScenePhysicsRecipe& recipe,
               physics::Configuration configuration,
               const std::shared_ptr<memory::Domain>& domain);
  ~WorldPhysics() = default;
  WorldPhysics(const WorldPhysics&) = delete;
  WorldPhysics& operator=(const WorldPhysics&) = delete;
  WorldPhysics(WorldPhysics&&) = delete;
  WorldPhysics& operator=(WorldPhysics&&) = delete;
  physics::Resources resources() { return world_.resources(); }
  physics::Queries queries() const { return world_.queries(); }
  physics::Mutations mutations() { return world_.mutations(); }
  void advance(const world::WorldFrame& frame) { world_.advance(frame); }
  // Publish solved character hit geometry without advancing or flushing forces.
  void publish_queries(const world::WorldFrame& frame) {
    world_.publish_queries(frame);
  }
  static constexpr std::uint16_t scenery_layers() {
    return (1U << environment_scenery_layer) | (1U << combined_scenery_layer);
  }
  std::optional<WorldPhysicsReceiver> receiver(
      const physics::ReceiverId& receiver) const;
  physics::Material character_material() const { return character_material_; }

 private:
  struct Receiver {
    physics::BodyId body;
    WorldPhysicsReceiver value;
  };
  physics::Runtime runtime_;
  physics::World world_;
  memory::Vector<Receiver> receivers_;
  physics::Material character_material_;
};
}  // namespace bw::game
