#ifndef BW_WORLD_TICK_FUNCTION_HPP
#define BW_WORLD_TICK_FUNCTION_HPP

#include "bw/world/clocks.hpp"

#include <cstdint>

// Responsibility: the work an object or a part registers to run each tick
// (docs/architecture/world.md, Tick function, Stage, Prerequisite;
// invariants 5 and 6). The world (bw/world/world.hpp) orders and calls
// them; this header is what the owner of such work implements.
//
// Contract.
//   1. The stages run in the order of Stage's enumerators.
//   2. A tick function is called at most once a tick, with the tick's
//      clocks (clocks.step is the step) and the stage it runs in, which is
//      its own stage or a later one (world.hpp, contract 4).
//   3. The world observes a tick function and never owns it: its owner
//      keeps it alive from registration until the world is destroyed, and
//      does not move it.
//
// Core guidelines read: C.121 (a pure abstract interface), C.35 (a
// protected non-virtual destructor: the world never deletes through it),
// R.3 (the world holds an observer).
namespace bw::world {

enum class Stage : std::uint8_t {
  BeforeSimulation,
  SimulationStart,
  DuringSimulation,
  SimulationEnd,
  AfterSimulation,
  Late,
};
inline constexpr std::uint32_t kStageCount = 6;

struct TickContext {
  Clocks clocks{};
  Stage stage{Stage::BeforeSimulation};
};

class TickFunction {
public:
  virtual void tick(const TickContext& context) = 0;

protected:
  TickFunction() = default;
  TickFunction(const TickFunction&) = default;
  TickFunction& operator=(const TickFunction&) = default;
  ~TickFunction() = default;
};

struct TickFunctionId {
  std::uint32_t value{0xFFFFFFFFU};
  [[nodiscard]] friend constexpr bool operator==(TickFunctionId, TickFunctionId) noexcept = default;
};

} // namespace bw::world
#endif
