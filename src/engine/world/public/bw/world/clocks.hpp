#ifndef BW_WORLD_CLOCKS_HPP
#define BW_WORLD_CLOCKS_HPP

// Responsibility: the world's clocks (docs/architecture/world.md,
// invariant 3). The world advances them by the step it is handed and reads
// no clock of the machine.
//
// Contract.
//   1. advance(clocks, step) adds `step` to each of the four clocks and
//      stores it as clocks.step, the step of the tick in progress.
//      Precondition: step is finite and not negative.
//   2. A new Clocks reads 0 on all four and a step of 0.
//
// The four differ only under pause and dilation, which have no caller:
// world time and audio time stop while paused, and world time and
// unpaused time advance by the dilated step. Until then they advance
// together.
namespace bw::world {

struct Clocks {
  // Seconds. World time is what gameplay reads.
  double world{};
  double unpaused{};
  double real{};
  double audio{};
  // The step of the tick in progress, seconds.
  double step{};
};

constexpr void advance(Clocks& clocks, double step) noexcept {
  clocks.real += step;
  clocks.audio += step;
  clocks.step = step;
  clocks.unpaused += step;
  clocks.world += step;
}

} // namespace bw::world
#endif
