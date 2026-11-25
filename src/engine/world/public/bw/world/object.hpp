#ifndef BW_WORLD_OBJECT_HPP
#define BW_WORLD_OBJECT_HPP

#include "bw/math/rotation/quaternion.hpp"
#include "bw/math/vector/vector3.hpp"

#include <cstdint>

// Responsibility: what names an object of the world and where it is
// (docs/architecture/world.md, Object: "one thing in the world with a
// transform, a lifetime and parts"; invariant 1: the world owns the
// transform and the lifetime of every object).
//
// Contract.
//   1. A Transform is a location in metres and a rotation, a unit
//      quaternion. The world's axes are right-handed with +y up. An
//      object's forward is its rotation applied to (kObjectForwardX,
//      kObjectForwardY, kObjectForwardZ), which is -z, the way the
//      game's character meshes face in their own axes, and its up is +y
//      at the identity rotation. The yaw an object faces is read with
//      bw/camera/pose.hpp, facing_yaw_degrees.
//   2. An ObjectId names one object of one World for that object's life;
//      kNoObject names none.
namespace bw::world {

struct Transform {
  math::Vector3f location{math::zero_init};
  math::Quaternion4f rotation{math::identity_init};
};

// The direction an object faces at the identity rotation.
inline constexpr float kObjectForwardX = 0.0F;
inline constexpr float kObjectForwardY = 0.0F;
inline constexpr float kObjectForwardZ = -1.0F;

struct ObjectId {
  std::uint32_t value{0xFFFFFFFFU};
  [[nodiscard]] friend constexpr bool operator==(ObjectId, ObjectId) noexcept = default;
};
inline constexpr ObjectId kNoObject{};

} // namespace bw::world
#endif
