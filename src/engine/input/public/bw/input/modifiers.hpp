#ifndef BW_INPUT_MODIFIERS_HPP
#define BW_INPUT_MODIFIERS_HPP

#include <cstdint>

// Responsibility: an action's value and the steps that change one
// (docs/architecture/input.md, Modifier; invariants 5 and 6). A modifier
// is data on a mapping or an action; apply_modifier is the one function
// that runs it. The kinds are the ones the game's mappings use.
//
// Contract.
//   1. as_type(value, type) brings a value to a value type: Switch keeps
//      x, with y and z 0; Axis1D keeps x; Axis2D keeps x and y; Axis3D
//      keeps all three.
//   2. apply_modifier(modifier, value, type) is as_type of the kind's
//      result:
//      DeadZone   with L = lower, U = upper and
//                 f(v) = min(1, max(0, |v| - L) / (U - L)) * sign(v):
//                 Axial applies f to each axis alone; Radial keeps the
//                 value's direction and applies f to its length over the
//                 type's axes. A Switch value passes unchanged.
//                 Precondition: 0 <= L < U.
//      Scale      each axis times the factor's axis; a Switch value passes
//                 unchanged.
//      Negate     the sign of each chosen axis flipped.
//      SwapXY     x and y exchanged.
//   3. A list of modifiers runs in order, each on the result of the one
//      before (player_input.hpp runs a mapping's list, then the action's).
namespace bw::input {

enum class ValueType : std::uint8_t { Switch, Axis1D, Axis2D, Axis3D };

struct ActionValue {
  float x{};
  float y{};
  float z{};
  [[nodiscard]] friend constexpr bool operator==(const ActionValue&,
                                                 const ActionValue&) noexcept = default;
};

[[nodiscard]] constexpr ActionValue as_type(ActionValue value, ValueType type) noexcept {
  switch (type) {
  case ValueType::Switch:
  case ValueType::Axis1D:
    return ActionValue{value.x, 0.0F, 0.0F};
  case ValueType::Axis2D:
    return ActionValue{value.x, value.y, 0.0F};
  case ValueType::Axis3D:
    return value;
  }
  return value;
}

enum class ModifierKind : std::uint8_t { DeadZone, Scale, Negate, SwapXY };
enum class DeadZoneKind : std::uint8_t { Axial, Radial };

struct Modifier {
  ModifierKind kind{ModifierKind::Scale};
  // DeadZone.
  DeadZoneKind dead_zone{DeadZoneKind::Axial};
  float lower{};
  float upper{1.0F};
  // Scale.
  ActionValue factor{1.0F, 1.0F, 1.0F};
  // Negate.
  bool negate_x{};
  bool negate_y{};
  bool negate_z{};
};

// A dead zone's two thresholds; precondition 0 <= lower < upper.
struct DeadZoneRange {
  float lower{};
  float upper{1.0F};
};
[[nodiscard]] constexpr Modifier dead_zone(DeadZoneKind kind, DeadZoneRange range) noexcept {
  Modifier modifier{};
  modifier.kind = ModifierKind::DeadZone;
  modifier.dead_zone = kind;
  modifier.lower = range.lower;
  modifier.upper = range.upper;
  return modifier;
}
[[nodiscard]] constexpr Modifier scale(ActionValue factor) noexcept {
  Modifier modifier{};
  modifier.kind = ModifierKind::Scale;
  modifier.factor = factor;
  return modifier;
}
[[nodiscard]] constexpr Modifier negate(bool x, bool y, bool z) noexcept {
  Modifier modifier{};
  modifier.kind = ModifierKind::Negate;
  modifier.negate_x = x;
  modifier.negate_y = y;
  modifier.negate_z = z;
  return modifier;
}
[[nodiscard]] constexpr Modifier swap_xy() noexcept {
  Modifier modifier{};
  modifier.kind = ModifierKind::SwapXY;
  return modifier;
}

[[nodiscard]] ActionValue apply_modifier(const Modifier& modifier, ActionValue value,
                                         ValueType type) noexcept;

} // namespace bw::input
#endif
