#ifndef BW_ENGINE_RUNTIME_STEP_HPP
#define BW_ENGINE_RUNTIME_STEP_HPP

// Responsibility: the step, the time one world tick covers
// (docs/architecture/world.md, invariant 2: the runtime loop makes it,
// measured from its clock or fixed when a fixed step is set, and hands it
// to the tick; the world reads no clock). A pure function a host's loop
// calls once a frame with the two clock readings it already has.
//
// Contract.
//   1. make_step(settings, previous_seconds, now_seconds) is, in seconds:
//      settings.fixed_step when it is above 0, whatever the clock read;
//      otherwise now_seconds - previous_seconds. Then, when
//      settings.longest_step is above 0, the smaller of that and
//      settings.longest_step. A fixed step is not held.
//   2. Precondition: now_seconds >= previous_seconds, both readings of one
//      monotonic clock; fixed_step and longest_step are finite and not
//      negative. The result is then finite and not negative.
//
// Core guidelines read: I.1 (every input is an argument; no clock is read
// here), F.4 (constexpr), E.7 (the precondition is stated).
namespace bw::engine {

struct StepSettings {
  // Seconds; 0 measures the step from the clock.
  double fixed_step{};
  // Seconds; 0 leaves the step unbounded.
  double longest_step{};
};

[[nodiscard]] constexpr double make_step(const StepSettings& settings, double previous_seconds,
                                         double now_seconds) noexcept {
  if (settings.fixed_step > 0.0) {
    return settings.fixed_step;
  }
  const double measured = now_seconds - previous_seconds;
  return settings.longest_step > 0.0 && measured > settings.longest_step ? settings.longest_step
                                                                         : measured;
}

} // namespace bw::engine
#endif
