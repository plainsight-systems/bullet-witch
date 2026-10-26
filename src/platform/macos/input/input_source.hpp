// Main-thread native physical input owner. Begin clears only per-pump edges,
// deltas and sampled controller axes; observe consumes a borrowed NSEvent
// during that call; finish polls devices and returns a copied sample. Focus
// loss clears all held/pressed input but preserves controller availability. No
// game actions, timing, profile or window ownership. Fixed state and no
// authored heap work; platform enumeration retains native framework cost.
// Ordering/reference: neutral-input-boundary.md. Both-phase Core I.1
// (#ri-explicit), C.9
// (#rc-private) separate capabilities; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retains inline
// history.
#pragma once
#include "bw/input/physical_state.hpp"
#include "gamepad.hpp"
@class NSEvent;
namespace bw::platform::macos {
class InputSource final {
 public:
  InputSource() = default;
  ~InputSource() = default;
  InputSource(const InputSource&) = delete;
  InputSource& operator=(const InputSource&) = delete;
  InputSource(InputSource&&) = delete;
  InputSource& operator=(InputSource&&) = delete;
  void begin() noexcept;
  void observe(NSEvent* event);
  input::PhysicalState finish(bool focused);

 private:
  input::PhysicalState physical_;
  GamepadButtonEdges pad_edges_;
};
}  // namespace bw::platform::macos
