// Static triangle registration and original segment-clearance query. Register
// validated collision definitions before querying; each definition and its
// byte owner must outlive this object. Registration applies original category
// and name exclusions, retains borrowed part references in the supplied domain,
// and commits one definition atomically. Queries allocate nothing, test
// original front faces (including coplanar edge contacts), select the closest
// hit, then subtract incidence-adjusted clearance. Invalid requests throw
// invalid_argument. Exclusive access is required while registering; frozen
// queries are reentrant. Work O(parts + candidate triangles) per query;
// metadata O(registered parts). Optimization: part bounds reject unrelated
// triangles without copying them. The original spatial grid can replace this
// bounded initial-view traversal without changing narrow-phase behavior.
// Dynamic actor collision is separate. Both MCP phases: R.2 (#rr-use-ptr) makes
// borrowed extents explicit; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// metadata storage. Original algorithm evidence:
// player-camera-initialization.md.
#pragma once
#include <functional>

#include "bw/formats/stage_definitions.hpp"

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
};
class StaticCollision {
 public:
  explicit StaticCollision(const std::shared_ptr<memory::Domain>& domain)
      : parts_(memory::Allocator<Part>(domain)) {}
  void add(const formats::CollisionDefinition& definition,
           CollisionSourceMode mode);
  SegmentClearanceResult clip_segment(const SegmentClearance& request) const;
  std::size_t part_count() const { return parts_.size(); }

 private:
  struct Part {
    std::reference_wrapper<const formats::CollisionPart> definition;
    std::uint32_t object_flags{};
  };
  memory::Vector<Part> parts_;
};
}  // namespace bw::scene
