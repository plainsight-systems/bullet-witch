// Prepare model-bind-space source triangles once, then update only copied
// affine transforms. R.1 (#rr-raii) pairs provider acquisition/retirement;
// CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// keeps topology conversion out of the frame path. Both corpora retrieved for
// implementation. The original EHIT query path is independent of ragdoll mass.
#include "actor_hit_geometry.hpp"

#include <bit>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/math/matrix.hpp"

namespace bw::game {
namespace {
constexpr unsigned kPartKindMask = 7U, kGroupShift = 8U, kGroupMask = 0xffU;
constexpr std::size_t kVertexBytes = 12, kFaceBytes = 8, kSurfaceOffset = 6;
constexpr std::uint32_t kGroupBits = 32;
math::Affine3<float> receiver_transform(const animation::Matrix& matrix) {
  if (!math::all_finite(matrix) || !math::inverse_affine(matrix)) {
    throw std::invalid_argument(
        "EHIT receiver needs a finite invertible affine skin transform");
  }
  return math::affine_part(matrix);
}
}  // namespace
ActorHitGeometry::ActorHitGeometry(
    physics::Resources resources,
    const formats::CollisionDefinition& definition,
    const std::shared_ptr<memory::Domain>& domain)
    : resources_(std::move(resources)),
      parts_(memory::Allocator<Part>(domain)) {
  parts_.reserve(definition.parts().size());
  memory::Vector<physics::Vector3> vertices{
      memory::Allocator<physics::Vector3>(domain)};
  memory::Vector<physics::Triangle> triangles{
      memory::Allocator<physics::Triangle>(domain)};
  try {
    for (const auto& part : definition.parts()) {
      if (((part.flags >> 2U) & kPartKindMask) != 1U) {
        throw std::invalid_argument("Unsupported deforming actor EHIT part");
      }
      const resources::detail::Reader vertex_reader(part.vertices);
      const resources::detail::Reader face_reader(part.triangles);
      vertices.resize(part.vertex_count);
      triangles.resize(part.triangle_count);
      for (std::size_t index = 0; index < vertices.size(); ++index) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
          vertices.at(index).at(axis) = std::bit_cast<float>(
              vertex_reader.u32(index * kVertexBytes + axis * 4));
        }
      }
      for (std::size_t index = 0; index < triangles.size(); ++index) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
          triangles.at(index).vertices.at(corner) =
              face_reader.u16(index * kFaceBytes + corner * 2);
        }
        triangles.at(index).surface =
            face_reader.u16(index * kFaceBytes + kSurfaceOffset);
      }
      const auto shape =
          resources_.create_shape(physics::TriangleMesh{vertices, triangles});
      parts_.push_back(
          {shape, part.bone, (part.flags >> kGroupShift) & kGroupMask});
    }
  } catch (...) {
    retire();
    throw;
  }
}
ActorHitGeometry::~ActorHitGeometry() { retire(); }
void ActorHitGeometry::retire() {
  if (resources_.alive()) {
    for (const auto& part : parts_) resources_.remove_shape(part.shape);
  }
  parts_.clear();
}
ActorHitInstance::ActorHitInstance(
    physics::Resources resources, physics::Mutations mutations,
    std::shared_ptr<const ActorHitGeometry> geometry,
    std::span<const std::uint32_t, kGroupWordCount> groups,
    const animation::SkeletonPose& pose, std::uint64_t actor,
    std::uint8_t layer, const std::shared_ptr<memory::Domain>& domain)
    : resources_(std::move(resources)),
      mutations_(std::move(mutations)),
      geometry_(std::move(geometry)),
      parts_(memory::Allocator<Part>(domain)),
      transforms_(memory::Allocator<physics::QueryTransform>(domain)),
      flags_(memory::Allocator<physics::QueryFlags>(domain)) {
  const auto skin_world = pose.skin_transforms();
  if (!geometry_ || actor == 0 || layer >= physics::layer_count) {
    throw std::invalid_argument("Invalid actor hit receiver admission");
  }
  parts_.reserve(geometry_->parts().size());
  transforms_.reserve(geometry_->parts().size());
  flags_.reserve(geometry_->parts().size());
  try {
    for (const auto& part : geometry_->parts()) {
      if ((groups[part.group / kGroupBits] &
           (std::uint32_t{1} << (part.group % kGroupBits))) == 0) {
        continue;
      }
      if (part.bone >= skin_world.size()) {
        throw std::invalid_argument("EHIT receiver references missing bone");
      }
      // A bone-bound mesh retains authored scale/shear without a solver body.
      physics::QueryInstanceDescription instance;
      instance.shape = part.shape;
      instance.transform = receiver_transform(skin_world[part.bone]);
      instance.layer = layer;
      instance.user_data = actor;
      const auto id = resources_.create_query_instance(instance);
      parts_.push_back({id, part.bone});
      transforms_.push_back({id, instance.transform});
      flags_.push_back({id, 0});
    }
  } catch (...) {
    retire();
    throw;
  }
}
ActorHitInstance::~ActorHitInstance() { retire(); }
void ActorHitInstance::publish(const animation::SkeletonPose& pose) {
  const auto skin_world = pose.skin_transforms();
  // Validate the complete batch before enqueueing any pose mutations.
  for (std::size_t index = 0; index < parts_.size(); ++index) {
    const auto bone = parts_.at(index).bone;
    if (bone >= skin_world.size()) {
      throw std::invalid_argument("EHIT update references missing bone");
    }
    transforms_.at(index).transform = receiver_transform(skin_world[bone]);
  }
  mutations_.query_transforms(transforms_);
}
void ActorHitInstance::query_flags(std::uint32_t mask, bool enabled) {
  const auto value = enabled ? query_flags_ | mask : query_flags_ & ~mask;
  if (value == query_flags_) return;
  for (auto& flags : flags_) flags.flags = value;
  mutations_.query_flags(flags_);
  query_flags_ = value;
}
void ActorHitInstance::retire() {
  if (resources_.alive()) {
    for (const auto& part : parts_) {
      resources_.remove_query_instance(part.instance);
    }
  }
  parts_.clear();
  transforms_.clear();
  flags_.clear();
}
}  // namespace bw::game
