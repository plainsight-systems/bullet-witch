#ifndef BW_CAMERA_RIG_HPP
#define BW_CAMERA_RIG_HPP

#include <cstdint>
#include <span>
#include <string_view>

// Responsibility: a rig as data (docs/architecture/camera.md, Node, Rig,
// Transition, Blend, Input slot): an ordered list of nodes that makes one
// pose, and the transitions that say with which blend the view goes from
// one rig to another. The game writes rigs as tables of these; the
// evaluator (bw/camera/evaluator.hpp) loads and runs them. The node and
// blend kinds are the ones the game's rigs use.
//
// Contract.
//   1. A rig's nodes run in order, each on the pose the one before left:
//      Attach  sets the pose's location to the owner's location, and with
//              `rotation` its yaw to the owner's facing yaw and its pitch
//              to 0 (invariant 9).
//      Offset  moves the location by `offset`: in World space along the
//              world's axes; in Pose space x along the pose's right, y
//              along its up, z along its forward (pose.hpp, contract 3).
//      Boom    sets the pose's yaw and pitch to its input slot's value,
//              then moves the location from where it was, the pivot, by
//              `offset` in that rotated frame: x to the right, y up, z
//              forward, so a camera behind the pivot has a negative z
//              (invariant 10).
//      Lens    sets the field of view, the sensor, near and far.
//   2. A boom's input slot accumulates a look action (invariant 12): each
//      evaluation it adds, per axis, the action's value times the slot's
//      speed times the step, the action's x to the yaw and its y to the
//      pitch; then brings an axis whose `wrap` is above 0 into [0, wrap);
//      then holds an axis to its clamp's lowest and highest where each is
//      on. The speed is in degrees a second for an action value of 1. How
//      a slot starts, and the speed while two rigs blend, are the
//      evaluator's (evaluator.hpp, contract 5).
//   3. blend_factor(blend, fraction), fraction the blend's elapsed time
//      over its time held to [0, 1]: Pop is 1 at every fraction; Easing
//      with CubicOut is 1 - (1 - fraction)^3. A blend whose time is 0 or
//      less is at fraction 1 from its first frame.
//   4. A transition matches a change from rig `from` to rig `to` when its
//      `from` is empty or names the first and its `to` is empty or names
//      the second.
//
// Core guidelines read: Enum.3, I.1 (a rig is every input of its pose but
// the context and the action), Con.5 (the tables are plain data).
// Performance guidelines read: COPY.4 (nodes are small plain values run in
// order).
namespace bw::camera {

struct Offset3 {
  float x{};
  float y{};
  float z{};
};

// One axis's limits of an input slot: the lowest and highest value, each
// only when its switch is on.
struct AxisClamp {
  float lowest{};
  float highest{};
  bool clamp_lowest{};
  bool clamp_highest{};
};

struct InputSlot {
  // The look action's name: an Axis2D action whose x feeds the yaw and y
  // the pitch.
  std::string_view action{};
  // Degrees a second for an action value of 1.
  float speed_yaw{1.0F};
  float speed_pitch{1.0F};
  // Above 0, the value is brought into [0, wrap) before the clamp.
  float wrap_yaw{};
  float wrap_pitch{};
  AxisClamp clamp_yaw{};
  AxisClamp clamp_pitch{};
};

struct LensValues {
  // Horizontal, degrees, across the sensor (pose.hpp, contract 2).
  float field_of_view_degrees{};
  float sensor_width{};
  float sensor_height{};
  float near_plane{};
  float far_plane{};
};

enum class NodeKind : std::uint8_t { Attach, Offset, Boom, Lens };
enum class OffsetSpace : std::uint8_t { World, Pose };

struct Node {
  NodeKind kind{NodeKind::Attach};
  // Attach.
  bool rotation{};
  // Offset and Boom.
  Offset3 offset{};
  // Offset.
  OffsetSpace space{OffsetSpace::World};
  // Boom.
  InputSlot slot{};
  // Lens.
  LensValues lens{};
};

[[nodiscard]] constexpr Node attach(bool rotation) noexcept {
  Node node{};
  node.kind = NodeKind::Attach;
  node.rotation = rotation;
  return node;
}
[[nodiscard]] constexpr Node offset(Offset3 by, OffsetSpace space) noexcept {
  Node node{};
  node.kind = NodeKind::Offset;
  node.offset = by;
  node.space = space;
  return node;
}
[[nodiscard]] constexpr Node boom(Offset3 by, const InputSlot& slot) noexcept {
  Node node{};
  node.kind = NodeKind::Boom;
  node.offset = by;
  node.slot = slot;
  return node;
}
[[nodiscard]] constexpr Node lens(LensValues values) noexcept {
  Node node{};
  node.kind = NodeKind::Lens;
  node.lens = values;
  return node;
}

enum class BlendKind : std::uint8_t { Pop, Easing };
enum class EasingCurve : std::uint8_t { CubicOut };

struct Blend {
  BlendKind kind{BlendKind::Pop};
  EasingCurve curve{EasingCurve::CubicOut};
  // Seconds; unused by Pop.
  float time{};
};

// Precondition: 0 <= fraction <= 1.
[[nodiscard]] constexpr float blend_factor(const Blend& blend, float fraction) noexcept {
  if (blend.kind == BlendKind::Pop) {
    return 1.0F;
  }
  const float rest = 1.0F - fraction;
  return 1.0F - rest * rest * rest;
}

struct Transition {
  // Rig names; empty matches any rig.
  std::string_view from{};
  std::string_view to{};
  Blend blend{};
};

struct RigDescription {
  std::string_view name{};
  std::span<const Node> nodes{};
  std::span<const Transition> enter{};
  std::span<const Transition> exit{};
};

} // namespace bw::camera
#endif
