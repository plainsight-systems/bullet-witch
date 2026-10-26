// Engine authority for published world intervals and admitted simulation time.
// The game supplies phase/input admission; native wall sampling and pause-menu
// policy are separate owners. Algorithm: validate presentation delta/scale ->
// multiply once -> publish immutable interval and phase -> accumulate only an
// admitted normal-world interval. No second cap, catch-up or fixed-step loop.
// Control frames retain nonzero scaled delta for their separate consumers.
// Local animation cursors never write world time. Single owner thread; fixed
// storage, no allocation, constant work. Invalid inputs preserve state.
// Design Core C.9 (#rc-private) prevents competing writers; I.1 (#ri-explicit)
// makes phase admission explicit; performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) bounds steady-state work.
// Implementation: Core E.4 (#re-design-invariants) validates candidates before
// publication; MEM.9 was reread for allocation-free bounded update state.
#pragma once
#include <cstdint>

namespace bw::world {
struct TickAdmission {
  bool simulation{true};
  bool hardware_input{true};
};
class WorldClock;
// Borrowed issuer identity: consume/copy only while the issuing WorldClock
// exists. Frames must retire before that owner; they are not transferable
// across mission destruction or reconstruction at the same address. Value
// assignment replaces an observation, never clock state. No asynchronous
// lifetime is implied.
class WorldFrame {
 public:
  std::uint64_t sequence() const noexcept { return sequence_; }
  float delta_seconds() const noexcept { return delta_; }
  double simulation_seconds() const noexcept { return simulation_seconds_; }
  TickAdmission admission() const noexcept { return admission_; }
  bool belongs_to(const WorldClock& clock) const noexcept {
    return owner_ == &clock;
  }

 private:
  friend class WorldClock;
  WorldFrame(const WorldClock& owner, float delta, TickAdmission admission);
  const WorldClock* owner_;
  std::uint64_t sequence_;
  float delta_;
  double simulation_seconds_;
  TickAdmission admission_;
};
class WorldClock {
 public:
  WorldClock() = default;
  WorldClock(const WorldClock&) = delete;
  WorldClock& operator=(const WorldClock&) = delete;
  WorldClock(WorldClock&&) = delete;
  WorldClock& operator=(WorldClock&&) = delete;
  ~WorldClock() = default;
  WorldFrame publish(float presentation_seconds, TickAdmission admission);
  // An authored change affects the next publication, never a retained frame.
  void set_scale(float scale);
  float scale() const noexcept { return scale_; }
  double simulation_seconds() const noexcept { return simulation_seconds_; }

 private:
  friend class WorldFrame;
  std::uint64_t sequence_{};
  float scale_{1};
  double simulation_seconds_{};
};
}  // namespace bw::world
