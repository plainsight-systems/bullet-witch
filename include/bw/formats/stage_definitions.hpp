// Label name/track accessors apply both-phase R.2 (#rr-use-ptr) to checked
// borrowed records and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) to allocation-free label lookup.
// Original load-time collision/area/material-animation definitions. Inputs are
// immutable borrowed bytes; callers retain their archive throughout every view.
// Metadata alone uses the injected domain. Constructors validate
// directory/count ranges and replace original in-place x86 pointer rebasing
// with bounded offsets. No physics scene, sampling clock, actor or GPU resource
// is constructed here. Malformed layouts throw resources::Error; unknown
// versions remain unsupported. Algorithm: validate fixed header -> bound each
// source directory -> validate referenced records -> retain typed views.
// Collision additionally validates all face indices. Cost O(records + faces),
// metadata O(records), no bulk copies. Both MCP phases: SL.con.3 (#rsl-bounds)
// for checked spans; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) for
// domain ownership. Native constructor evidence: stage-content-decoding.md.
// Background body associations apply both-phase C.32 (#rc-dtor-ptr) and
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md): load-owned
// integer mappings borrow no physics runtime and allocate only at construction.
// Optional authored local lights retain copied scalar data and a signed source
// joint. Decode only enabled records; no scene registration occurs here.
// Both MCP phases I.1 (ri-explicit) and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) separate immutable setup from
// publication.
#pragma once

#include "bw/formats/common.hpp"
#include "bw/memory/allocator.hpp"

namespace bw::formats {
struct CollisionPart {
  std::string_view name;
  std::array<float, 3> center{}, extent{};
  float radius{};
  std::uint32_t flags{}, vertex_count{}, triangle_count{};
  // Float3 vertices and three uint16 indices plus one uint16 flag per face.
  std::span<const std::uint8_t> vertices, triangles;
};
class CollisionDefinition {
 public:
  static CollisionDefinition parse(
      std::span<const std::uint8_t> bytes,
      const std::shared_ptr<memory::Domain>& domain);
  const auto& parts() const { return parts_; }

 private:
  explicit CollisionDefinition(const std::shared_ptr<memory::Domain>& domain)
      : parts_(memory::Allocator<CollisionPart>(domain)) {}
  memory::Vector<CollisionPart> parts_;
};

// PHIT construction resolves the original pointer graph without creating a
// physics SDK scene. Each relocation retains its source word and bounded
// target; later collision consumers interpret the native records rather than
// host pointers.
struct CollisionReference {
  std::uint32_t source_offset{}, target_offset{};
};
// Typed shape records borrow convex float3 bytes; local_pose is expanded to
// row-vector basis rows plus translation. Body index selects the matching
// authored placement matrix. Unsupported mesh kinds remain explicit.
// Both MCP phases: I.1 (ri-explicit) defines geometry/pose units; MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) limits metadata to
// load.
enum class PhysicsShapeKind : std::uint8_t {
  sphere,
  box,
  capsule,
  convex,
  triangle_mesh,
  triangle_mesh_alt
};
struct PhysicsShape {
  PhysicsShapeKind kind{};
  std::size_t body{};
  std::array<std::array<float, 3>, 4> local_pose{};
  std::array<float, 3> dimensions{};
  std::span<const std::uint8_t> convex_points;
};
struct PhysicsLocalLight {
  std::int32_t joint{-1};
  std::array<float, 3> diffuse{}, specular{}, attenuation{};
};
class PhysicsCollisionDefinition {
 public:
  static PhysicsCollisionDefinition parse(
      std::span<const std::uint8_t> bytes,
      const std::shared_ptr<memory::Domain>& domain);
  const auto& references() const { return references_; }
  // Original physics-body order maps each authored placement matrix to a
  // skeleton joint. Missing body records retain -1; no physics SDK is needed.
  const auto& body_joints() const { return body_joints_; }
  const auto& shapes() const { return shapes_; }
  const auto& local_light() const { return local_light_; }
  std::span<const std::uint8_t> bytes() const { return bytes_; }

 private:
  PhysicsCollisionDefinition(std::span<const std::uint8_t> bytes,
                             const std::shared_ptr<memory::Domain>& domain)
      : bytes_(bytes),
        references_(memory::Allocator<CollisionReference>(domain)),
        body_joints_(memory::Allocator<std::int32_t>(domain)),
        shapes_(memory::Allocator<PhysicsShape>(domain)) {}
  std::span<const std::uint8_t> bytes_;
  memory::Vector<CollisionReference> references_;
  memory::Vector<std::int32_t> body_joints_;
  memory::Vector<PhysicsShape> shapes_;
  std::optional<PhysicsLocalLight> local_light_;
};

class AreaDefinition {
 public:
  static AreaDefinition parse(std::span<const std::uint8_t> bytes,
                              const std::shared_ptr<memory::Domain>& domain);
  const auto& records() const { return records_; }

 private:
  explicit AreaDefinition(const std::shared_ptr<memory::Domain>& domain)
      : records_(memory::Allocator<std::span<const std::uint8_t>>(domain)) {}
  memory::Vector<std::span<const std::uint8_t>> records_;
};

// FLAN initialization binds time, channel-code and value regions. This view
// faithfully owns those load-time associations; evaluation is a separate owner.
struct ScalarAnimationDefinition {
  std::uint32_t version{}, frame_count{};
  std::span<const std::uint8_t> times, channels, values;
  static ScalarAnimationDefinition parse(std::span<const std::uint8_t> bytes);
};
struct MaterialAnimationTrack {
  std::span<const std::uint8_t> header;
  std::uint32_t first_data_offset{}, second_data_offset{};
};
class MaterialAnimationDefinition {
 public:
  static MaterialAnimationDefinition parse(
      std::span<const std::uint8_t> bytes,
      const std::shared_ptr<memory::Domain>& domain);
  const auto& tracks() const { return tracks_; }
  std::span<const std::uint8_t> bytes() const { return bytes_; }

 private:
  MaterialAnimationDefinition(std::span<const std::uint8_t> bytes,
                              const std::shared_ptr<memory::Domain>& domain)
      : bytes_(bytes),
        tracks_(memory::Allocator<MaterialAnimationTrack>(domain)) {}
  std::span<const std::uint8_t> bytes_;
  memory::Vector<MaterialAnimationTrack> tracks_;
};
struct MaterialAnimationLabels {
  std::uint32_t count{};
  std::span<const std::uint8_t> records;
  static MaterialAnimationLabels parse(std::span<const std::uint8_t> bytes);
  std::string_view name(std::size_t index) const;
  std::uint32_t track(std::size_t index) const;
};
}  // namespace bw::formats
