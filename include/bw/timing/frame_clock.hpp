// Engine presentation timing, independent of game state and device clocks.
// Borrow no services: the native host supplies monotonic timestamps and
// performs waits. Single owner thread. Algorithm: validate sample -> measure
// elapsed -> cap presentation delta -> advance deadline, rebasing missed
// deadlines without catch-up frames. Explicit rebase excludes loading/resource
// transaction time. Rejection preserves previous state. No allocation; constant
// work/storage. The original render-publication cap is 100 ms; target pacing is
// a constructor input, not a fixed simulation timestep. See
// docs/architecture/world-timing.md. Design Core C.9 (#rc-private) owns
// samples/deadlines, I.1 (#ri-explicit) separates native measurement from
// policy; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps frame work
// inline. Implementation: Core E.4 (#re-design-invariants) validates candidates
// before publication; MEM.9 was reread for allocation-free bounded update
// state. Ownership correction: Core I.4 (#ri-typed) and performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) make
// unique authority explicit. Moves are also forbidden: retained callbacks
// borrow this stable owner; timing snapshots remain cheap independent values.
#pragma once
#include <chrono>

namespace bw::timing {
struct FrameTiming {
  float wall_seconds{};
  float presentation_seconds{};
};
class FrameClock {
 public:
  using Clock = std::chrono::steady_clock;
  FrameClock(Clock::time_point started, Clock::duration target_period);
  FrameClock(const FrameClock&) = delete;
  FrameClock& operator=(const FrameClock&) = delete;
  FrameClock(FrameClock&&) = delete;
  FrameClock& operator=(FrameClock&&) = delete;
  ~FrameClock() = default;
  FrameTiming sample(Clock::time_point now);
  void rebase(Clock::time_point now);
  Clock::time_point deadline() const noexcept { return deadline_; }

 private:
  Clock::time_point previous_, deadline_;
  Clock::duration period_;
};
}  // namespace bw::timing
