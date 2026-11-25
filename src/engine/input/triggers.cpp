// The rules that decide whether a mapping fires (bw/input/triggers.hpp).
#include "bw/input/triggers.hpp"

#include "bw/input/modifiers.hpp"

#include <cmath>

namespace bw::input {

bool update_trigger(const Trigger& trigger, TriggerMemory& memory, ActionValue value) noexcept {
  const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
  const bool actuated = length >= trigger.threshold;
  bool fires = false;
  switch (trigger.kind) {
  case TriggerKind::Down:
    fires = actuated;
    break;
  case TriggerKind::Pressed:
    fires = actuated && !memory.was_actuated;
    break;
  }
  memory.was_actuated = actuated;
  return fires;
}

} // namespace bw::input
