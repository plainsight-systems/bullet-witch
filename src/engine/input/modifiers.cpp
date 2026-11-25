// The steps that change an action's value (bw/input/modifiers.hpp).
#include "bw/input/modifiers.hpp"

#include <algorithm>
#include <cmath>

namespace bw::input {
namespace {

// min(1, max(0, |v| - lower) / (upper - lower)) * sign(v).
float dead_zone_axis(float value, float lower, float upper) noexcept {
  const float scaled = std::min(1.0F, std::max(0.0F, std::abs(value) - lower) / (upper - lower));
  return value < 0.0F ? -scaled : scaled;
}

ActionValue apply_dead_zone(const Modifier& modifier, ActionValue value, ValueType type) noexcept {
  if (type == ValueType::Switch) {
    return value;
  }
  if (modifier.dead_zone == DeadZoneKind::Axial) {
    return ActionValue{dead_zone_axis(value.x, modifier.lower, modifier.upper),
                       dead_zone_axis(value.y, modifier.lower, modifier.upper),
                       dead_zone_axis(value.z, modifier.lower, modifier.upper)};
  }
  const ActionValue typed = as_type(value, type);
  const float length = std::sqrt(typed.x * typed.x + typed.y * typed.y + typed.z * typed.z);
  if (length == 0.0F) {
    return ActionValue{};
  }
  const float factor = dead_zone_axis(length, modifier.lower, modifier.upper) / length;
  return ActionValue{typed.x * factor, typed.y * factor, typed.z * factor};
}

} // namespace

ActionValue apply_modifier(const Modifier& modifier, ActionValue value, ValueType type) noexcept {
  ActionValue result = value;
  switch (modifier.kind) {
  case ModifierKind::DeadZone:
    result = apply_dead_zone(modifier, value, type);
    break;
  case ModifierKind::Scale:
    // A Switch value passes unchanged.
    if (type == ValueType::Switch) {
      break;
    }
    result = ActionValue{value.x * modifier.factor.x, value.y * modifier.factor.y,
                         value.z * modifier.factor.z};
    break;
  case ModifierKind::Negate:
    result =
        ActionValue{modifier.negate_x ? -value.x : value.x, modifier.negate_y ? -value.y : value.y,
                    modifier.negate_z ? -value.z : value.z};
    break;
  case ModifierKind::SwapXY:
    result = ActionValue{value.y, value.x, value.z};
    break;
  }
  return as_type(result, type);
}

} // namespace bw::input
