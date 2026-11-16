#ifndef BW_INPUT_TRIGGERS_HPP
#define BW_INPUT_TRIGGERS_HPP

#include "bw/input/modifiers.hpp"

#include <cstdint>

// Responsibility: the rules that decide from a mapping's modified value
// whether its action fires (docs/architecture/input.md, Trigger; invariant
// 7). A trigger is data on a mapping; update_trigger is the one function
// that runs it, with the memory the mapping keeps for it. The kinds are
// the ones the game's mappings use.
//
// Contract.
//   1. A value is actuated for a trigger when its length is at least the
//      trigger's threshold.
//   2. update_trigger(trigger, memory, value) returns whether the trigger
//      fires this frame, then remembers whether `value` was actuated:
//      Down     fires while the value is actuated.
//      Pressed  fires on the frame the value is actuated and the value
//               of the frame before was not.
//   3. fires_without_trigger(value): a mapping with no trigger fires while
//      its value is not zero, that is while its squared length is at
//      least kNonZeroSquared (player_input.hpp contract 4d says what the
//      action then does).
//   4. A mapping with several triggers fires when any of them fires.
namespace bw::input {

enum class TriggerKind : std::uint8_t { Down, Pressed };

struct Trigger {
  TriggerKind kind{TriggerKind::Down};
  float threshold{0.5F};
};

// What a mapping keeps per trigger between frames.
struct TriggerMemory {
  bool was_actuated{};
};

inline constexpr float kNonZeroSquared = 1.0e-8F;

[[nodiscard]] bool update_trigger(const Trigger& trigger, TriggerMemory& memory,
                                  ActionValue value) noexcept;

[[nodiscard]] constexpr bool fires_without_trigger(ActionValue value) noexcept {
  return value.x * value.x + value.y * value.y + value.z * value.z >= kNonZeroSquared;
}

} // namespace bw::input
#endif
