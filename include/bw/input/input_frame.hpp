// Neutral device/window observations returned by one platform pump. Physical
// values include held state and this pump's presses; a down/up tap survives.
// No game commands, binding policy, owner pointers or repeat clocks cross this
// boundary. Availability is physical.controller_connected. Copying is bounded
// and allocation-free. Both-phase Core I.1 (#ri-explicit) gives the boundary an
// explicit value type; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retains inline
// storage.
#pragma once
#include "bw/input/physical_state.hpp"
namespace bw::input {
struct InputFrame {
  PhysicalState physical;
  bool close_requested{};
};
}  // namespace bw::input
