// Segment results retain the selected authored face's surface identifier as a
// value, separately from collision object flags. Missing metadata (analytic or
// generated prop geometry) remains absent; no material is guessed. Both phases
// I.1/E.4 expose result validity and CDSA.32/MEM.9 prepare the byte beside each
// immutable primitive, with no query allocation. Source: animation audio
// report. Static triangle registration and original segment-clearance query.
// Register validated collision definitions before querying; each definition and
// its byte owner must outlive this object. Registration applies original
// category and name exclusions, retains borrowed part references in the
// supplied domain, and commits one definition atomically. Queries allocate
// nothing, test original front faces (including coplanar edge contacts), select
// the closest hit, then subtract incidence-adjusted clearance. Invalid requests
// throw invalid_argument. Exclusive access is required while registering;
// frozen queries are reentrant. Work O(parts + candidate triangles) per query;
// metadata O(registered parts). Optimization: part bounds reject unrelated
// triangles without copying them. The original spatial grid can replace this
// bounded initial-view traversal without changing narrow-phase behavior.
// Character preparation copies decoded triangles and analytic props into a
// domain-owned BVH. Its fixed-scratch contact/support contract is declared in
// character_collision.hpp. Added world geometry must be prepared before camera
// or character queries; immutable readers share no scratch. Dynamic-body
// simulation is separate. Both MCP phases: R.2 (#rr-use-ptr) makes
// borrowed extents explicit; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// metadata storage. Original algorithm evidence:
// player-camera-initialization.md.
#pragma once
#include <functional>
#include <optional>

#include "bw/content/formats/stage_definitions.hpp"
#include "character_collision.hpp"
#include "collision_geometry.hpp"

namespace bw::scene {
enum class CollisionSourceMode : std::uint8_t {
  combined,
  character,
  environment
};
struct SegmentClearance {
  std::array<float, 3> origin{}, target{};
  float clearance{};
  std::uint32_t excluded_object_flags{};
};
struct SegmentClearanceResult {
  std::array<float, 3> position{};
  bool clipped{};
  std::optional<std::uint8_t> surface{};
};
class StaticCollision {
 public:
  explicit StaticCollision(const std::shared_ptr<memory::Domain>& domain)
      : parts_(memory::Allocator<Part>(domain)),
        additional_triangles_(memory::Allocator<CollisionTriangle>(domain)),
        capsules_(memory::Allocator<CollisionCapsule>(domain)),
        character_primitives_(memory::Allocator<CharacterPrimitive>(domain)),
        character_nodes_(memory::Allocator<CharacterNode>(domain)) {}
  void add(const formats::CollisionDefinition& definition,
           CollisionSourceMode mode);
  // World-space authored prop geometry, copied atomically per call. Triangle
  // winding is outward; a capsule with coincident endpoints is a sphere.
  // These setup registrations invalidate the prepared query hierarchy.
  void add_triangles(std::span<const CollisionTriangle> triangles);
  void add_capsules(std::span<const CollisionCapsule> capsules);
  SegmentClearanceResult clip_segment(const SegmentClearance& request) const;
  // Prepare after registration; adding geometry invalidates this publication.
  // Preparation requires exclusive access. Frozen character queries allocate
  // nothing and reject use before preparation rather than silently ignoring it.
  void prepare_character_queries();
  CharacterCollisionResult move_character(
      const CharacterCollisionRequest& request) const;
  std::size_t part_count() const { return parts_.size(); }

 private:
  struct Part {
    std::reference_wrapper<const formats::CollisionPart> definition;
    std::uint32_t object_flags{};
  };
  memory::Vector<Part> parts_;
  memory::Vector<CollisionTriangle> additional_triangles_;
  memory::Vector<CollisionCapsule> capsules_;
  struct CharacterPrimitive {
    std::array<std::array<float, 3>, 3> vertices{};
    std::array<float, 3> minimum{}, maximum{}, normal{};
    std::uint32_t flags{};
    std::size_t ordinal{};
    float radius{};
    bool capsule{};
    std::optional<std::uint8_t> surface{};
  };
  struct CharacterNode {
    std::array<float, 3> minimum{}, maximum{};
    std::size_t first{}, count{}, right{};
  };
  struct CharacterContact {
    std::array<float, 3> point{}, normal{};
    float depth{};
    std::size_t ordinal{};
    bool found{};
    std::optional<std::uint8_t> surface{};
  };
  struct BodyQuery {
    std::array<float, 3> position{};
    float radius{}, cylinder_height{};
    std::uint32_t excluded_flags{};
    float minimum_normal_y{-1};
  };
  CharacterContact body_contact(const BodyQuery& query) const;
  CharacterContact segment_contact(const std::array<float, 3>& origin,
                                   const std::array<float, 3>& target,
                                   std::uint32_t excluded_flags) const;
  memory::Vector<CharacterPrimitive> character_primitives_;
  memory::Vector<CharacterNode> character_nodes_;
  bool character_ready_{};
};
}  // namespace bw::scene
