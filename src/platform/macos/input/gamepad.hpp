// Native GameController action sampling and one-pump press ownership. Sample
// all controllers before deriving edges; keyboard taps may add explicit edges.
// A release rearms the next press. No repeats, game state, callbacks or heap
// allocation belong here. Caller serializes on the main thread and clears
// sampled holds on focus loss. I.1/E.4 make that boundary explicit; MEM.9 keeps
// the edge history fixed. Physical-position publication applies Core I.1
// (#ri-explicit); MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// retains fixed state. Both MCP phases keep game gestures out of sampling.
#pragma once
#include <cstdint>
@class GCExtendedGamepad;
namespace bw::platform::macos {
std::uint16_t gamepad_physical_buttons(GCExtendedGamepad* pad) noexcept;
class GamepadButtonEdges {
 public:
  std::uint16_t sample(std::uint16_t held,
                       std::uint16_t explicit_edges = 0) noexcept {
    const auto pressed =
        static_cast<std::uint16_t>(explicit_edges | (held & ~previous_));
    previous_ = held;
    return pressed;
  }

 private:
  std::uint16_t previous_{};
};
}  // namespace bw::platform::macos
