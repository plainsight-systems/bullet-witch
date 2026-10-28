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
// Lifetime-group design: Core I.1 (#ri-explicit) exposes one exclusive owner
// operation; MEM.9 keeps the 64-group mask fixed with allocation-free queries.
#pragma once
#include <functional>
#include <optional>

#include "bw/content/formats/stage_definitions.hpp"
#include "character_collision.hpp"
#include "collision_geometry.hpp"

namespace bw::scene {
// Application-owned registration groups; zero is the default geometry group.
// Values [0,63] fit the fixed lifetime mask. Geometry remains immutable while
// its owner changes visibility between query phases under exclusive access.
enum class CollisionGroup : std::uint8_t {};

enum class CollisionSourceMode : std::uint8_t {
  combined,
  character,
  environment
};
// Copied prepared geometry preserves the original bank/category and object
// filter metadata. Index order is the prepared hierarchy order, not asset
// order; ordinal preserves original tie-breaking identity. A missing category
// means analytic/generated prop geometry rather than an authored collision
// part.
struct PreparedCollisionGeometry {
  std::array<std::array<float, 3>, 3> vertices{};
  std::uint32_t object_flags{};
  std::size_t ordinal{};
  float radius{};
  bool capsule{};
  std::optional<std::uint8_t> surface{};
  CollisionSourceMode source{CollisionSourceMode::combined};
  std::optional<std::uint32_t> category{};
  bool query_geometry{true};
  std::optional<std::uint8_t> physics_material{};
  CollisionGroup group{};
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
  // Copied unit hit normal; zero when the segment has no retained contact.
  std::array<float, 3> normal{};
  // Copied registration ownership, not a pointer or surface/material guess.
  bool actor_owned{};
};
// Sum each admitted face's minimum translation at the original sphere center.
// This is a copied geometric observation, not iterative actor resolution.
struct SpherePenetrationResult {
  std::array<float, 3> translation{};
  bool penetrating{};
};
class StaticCollision {
 public:
  explicit StaticCollision(const std::shared_ptr<memory::Domain>& domain)
      : parts_(memory::Allocator<Part>(domain)),
        physics_only_parts_(memory::Allocator<Part>(domain)),
        physics_only_geometry_(
            memory::Allocator<PreparedCollisionGeometry>(domain)),
        additional_triangles_(memory::Allocator<RegisteredTriangle>(domain)),
        capsules_(memory::Allocator<RegisteredCapsule>(domain)),
        character_primitives_(memory::Allocator<CharacterPrimitive>(domain)),
        character_nodes_(memory::Allocator<CharacterNode>(domain)) {}
  void add(const formats::CollisionDefinition& definition,
           CollisionSourceMode mode);
  // World-space authored prop geometry, copied atomically per call. Triangle
  // winding is outward; a capsule with coincident endpoints is a sphere.
  // These setup registrations invalidate the prepared query hierarchy.
  void add_triangles(std::span<const CollisionTriangle> triangles,
                     CollisionGroup group = {}, bool actor_owned = false);
  void add_capsules(std::span<const CollisionCapsule> capsules,
                    CollisionGroup group = {}, bool actor_owned = false);
  // World-owner mutation only; const query borrowers cannot change this gate.
  // No allocation/rebuild; invalid group throws before changing the mask.
  void set_group_enabled(CollisionGroup group, bool enabled);
  SegmentClearanceResult clip_segment(const SegmentClearance& request) const;
  // Prepare after registration; adding geometry invalidates this publication.
  // Preparation requires exclusive access. Frozen character queries allocate
  // nothing and reject use before preparation rather than silently ignoring it.
  void prepare_character_queries();
  CharacterCollisionResult move_character(
      const CharacterCollisionRequest& request) const;
  // Prepared immutable geometry; fixed BVH scratch, no query allocation.
  // SL.con.3 bounds traversal; MEM.9 retains preparation-only storage.
  SpherePenetrationResult sphere_penetration(
      const std::array<float, 3>& center, float radius,
      std::uint32_t excluded_object_flags) const;
  bool overlaps_capsule(const std::array<float, 3>& start,
                        const std::array<float, 3>& end, float radius,
                        std::uint32_t excluded_object_flags) const;
  // Copy the authored face normal at minimum capsule support separation. Edge
  // separation normals are deliberately not substituted for triangle winding.
  // Equal depth uses immutable source ordinal; analytic contacts have no face.
  std::optional<std::array<float, 3>> capsule_face(
      const std::array<float, 3>& start, const std::array<float, 3>& end,
      float radius, std::uint32_t excluded_object_flags) const;
  std::size_t part_count() const { return parts_.size(); }
  // Preparation-only O(1) copied observations; reject unprepared geometry and
  // invalid indices. No raw assets, mutable hierarchy or decoder crosses here.
  // E.6 (#re-raii) retains storage ownership; MEM.9 keeps provider cooking in
  // preparation, with no per-frame geometry export.
  std::size_t geometry_count() const;
  PreparedCollisionGeometry geometry(std::size_t index) const;

 private:
  struct Part {
    std::reference_wrapper<const formats::CollisionPart> definition;
    std::uint32_t object_flags{};
    CollisionSourceMode source{};
    std::optional<std::uint8_t> physics_material{};
  };
  memory::Vector<Part> parts_;
  memory::Vector<Part> physics_only_parts_;
  memory::Vector<PreparedCollisionGeometry> physics_only_geometry_;
  struct RegisteredTriangle {
    CollisionTriangle geometry;
    CollisionGroup group{};
    bool actor_owned{};
  };
  struct RegisteredCapsule {
    CollisionCapsule geometry;
    CollisionGroup group{};
    bool actor_owned{};
  };
  memory::Vector<RegisteredTriangle> additional_triangles_;
  memory::Vector<RegisteredCapsule> capsules_;
  static std::uint64_t group_bit(CollisionGroup);
  bool group_enabled(CollisionGroup group) const {
    return (disabled_groups_ &
            (std::uint64_t{1} << static_cast<std::uint8_t>(group))) == 0;
  }
  std::uint64_t disabled_groups_{};
  struct CharacterPrimitive {
    std::array<std::array<float, 3>, 3> vertices{};
    std::array<float, 3> minimum{}, maximum{}, normal{};
    std::uint32_t flags{};
    std::size_t ordinal{};
    float radius{};
    bool capsule{};
    CollisionGroup group{};
    std::optional<std::uint8_t> surface{};
    CollisionSourceMode source{CollisionSourceMode::combined};
    std::optional<std::uint32_t> category{};
    std::optional<std::uint8_t> physics_material{};
    bool actor_owned{};
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
    std::array<float, 3> summed_translation{};
    bool actor_owned{};
    std::optional<std::array<float, 3>> face_normal{};
  };
  struct BodyQuery {
    std::array<float, 3> position{};
    float radius{}, cylinder_height{};
    std::uint32_t excluded_flags{};
    float minimum_normal_y{-1};
    std::optional<std::array<float, 3>> endpoint{};
    bool rank_support_separation{};
  };
  CharacterContact body_contact(const BodyQuery& query,
                                bool sum_translations = false) const;
  CharacterContact segment_contact(const std::array<float, 3>& origin,
                                   const std::array<float, 3>& target,
                                   std::uint32_t excluded_flags) const;
  memory::Vector<CharacterPrimitive> character_primitives_;
  memory::Vector<CharacterNode> character_nodes_;
  bool character_ready_{};
};
}  // namespace bw::scene
