// Prepare authored prop collision once per model and register frozen initial
// body poses. Input bytes are borrowed only during construction; prepared local
// triangles/capsules and reusable world scratch own their domain storage.
// Constructor validates geometry and builds convex hulls; registration requires
// one finite proper rigid matrix per source body, transforms local primitives,
// then copies them into the scene before query preparation. Invalid geometry,
// unsupported kinds or pose counts throw; failed setup requires discarding the
// incomplete scene. This single-threaded setup owner has no simulation API.
// Hull work scales with points times faces plus horizon-edge sorting;
// O(primitives) per instance, with no scratch growth after construction. Query
// allocation belongs to StaticCollision. Both MCP phases: I.1 (ri-explicit)
// defines borrowed/owned lifetimes and static-pose scope; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) prepares storage at
// load. Evidence and mature algorithm reference: placed-physics-collision.md.
#pragma once
#include "bw/content/formats/background_placement.hpp"
#include "bw/content/formats/stage_definitions.hpp"
#include "bw/spatial/static_collision.hpp"
namespace bw::game {
class PreparedPropCollision {
 public:
  PreparedPropCollision(const formats::PhysicsCollisionDefinition& definition,
                        const std::shared_ptr<memory::Domain>& domain);
  void register_instance(std::span<const formats::PlacementMatrix> body_world,
                         scene::StaticCollision& collision);
  std::size_t triangle_count() const { return triangles_.size(); }
  std::size_t capsule_count() const { return capsules_.size(); }

 private:
  struct Triangle {
    scene::CollisionTriangle geometry;
    std::size_t body{};
  };
  struct Capsule {
    scene::CollisionCapsule geometry;
    std::size_t body{};
  };
  std::size_t body_count_{};
  memory::Vector<Triangle> triangles_;
  memory::Vector<Capsule> capsules_;
  memory::Vector<scene::CollisionTriangle> world_triangles_;
  memory::Vector<scene::CollisionCapsule> world_capsules_;
};
}  // namespace bw::game
