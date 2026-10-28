// Authored behavior execution and its original staggered cadence. Prepared tree
// bytes/scorers, the world clock and native action host outlive this nonmoving
// owner. It owns only selection, four suspended call slots, action elapsed time
// and scheduling state. Actors retain animation/movement/weapon authority.
// Admit one world frame -> advance active action elapsed -> dispatch at source
// cadence -> reconsider completed/expired actions -> resolve authored labels ->
// exit/enter or suspend/restart authored action slots. Paused frames do none of
// it. Immutable requests borrow prepared definitions; callbacks are synchronous
// and must not reenter this owner. Invalid frames, indexes, cyclic controls and
// nested authored returns outside the admitted ordinary-soldier closure fail
// explicitly. No archive reads or frame allocations. Core I.11 (#ri-raw) makes
// every borrowed owner explicit. MEM.9 (guidelines/
// memory/MEM.9-embedded-allocate-at-init.md) fixes stack/schedule storage at
// init. Both design corpora read. Source and inference boundaries:
// first-combat-ai.md.
#pragma once
#include <array>

#include "behavior_selection.hpp"
#include "bw/timing/world_clock.hpp"

namespace bw::game {
struct BehaviorActionState {
  bool completed{}, can_reconsider{true}, force_every_frame{};
  std::uint8_t completion_code{};
};
struct BehaviorActionRequest {
  const formats::BehaviorTreeDefinition* definition{};
  std::size_t node{};
};
struct BehaviorActionFrame {
  float delta_seconds{}, elapsed_seconds{};
};
class BehaviorActions {
 public:
  BehaviorActions() = default;
  virtual ~BehaviorActions() = default;
  BehaviorActions(const BehaviorActions&) = delete;
  BehaviorActions& operator=(const BehaviorActions&) = delete;
  BehaviorActions(BehaviorActions&&) = delete;
  BehaviorActions& operator=(BehaviorActions&&) = delete;
  virtual BehaviorActionState state(std::size_t slot) const = 0;
  virtual void enter(std::size_t slot, const BehaviorActionRequest&) = 0;
  virtual void advance(std::size_t slot, BehaviorActionFrame) = 0;
  virtual void exit(std::size_t slot) = 0;
  virtual void suspend(std::size_t slot) = 0;
  virtual void resume(std::size_t slot) = 0;
};
class BehaviorSchedule {
 public:
  BehaviorSchedule() = default;
  BehaviorSchedule(const BehaviorSchedule&) = delete;
  BehaviorSchedule& operator=(const BehaviorSchedule&) = delete;
  BehaviorSchedule(BehaviorSchedule&&) = delete;
  BehaviorSchedule& operator=(BehaviorSchedule&&) = delete;
  ~BehaviorSchedule() = default;
  std::uint8_t next_evaluation();
  std::uint8_t next_dispatch() noexcept;
  void advance();

 private:
  static constexpr std::size_t kSchedulingSlots = 60;
  std::array<std::uint32_t, kSchedulingSlots> counts_{};
  std::uint8_t dispatch_{};
};
struct BehaviorLabelQuery {
  std::int32_t depth{}, label{};
};
class BehaviorRuntime {
 public:
  BehaviorRuntime(const world::WorldClock&, BehaviorSchedule&,
                  BehaviorActions&);
  BehaviorRuntime(const BehaviorRuntime&) = delete;
  BehaviorRuntime& operator=(const BehaviorRuntime&) = delete;
  BehaviorRuntime(BehaviorRuntime&&) = delete;
  BehaviorRuntime& operator=(BehaviorRuntime&&) = delete;
  ~BehaviorRuntime() = default;
  // Explicit lifecycle: start retires prior action slots before selecting from
  // the one-based authored entry. stop must run before the action host retires.
  void start(const formats::BehaviorTreeDefinition&, BehaviorScoring&,
             std::size_t one_based_entry);
  void stop();
  // Source event arbitration compares current action/entry and pending
  // priority; an admitted event is consumed after the current action's next
  // world update. Empty authored entries remain no-ops. This grants no
  // action/clock mutation.
  bool request_event(std::size_t one_based_entry, bool target_known);
  void advance(const world::WorldFrame&);
  std::optional<std::size_t> current_node() const noexcept;
  float elapsed_seconds() const noexcept;
  bool previous_labels(BehaviorLabelQuery) const noexcept;
  std::size_t active_slot() const noexcept {
    return std::span{slots_}[depth_].host_slot;
  }

 private:
  struct Slot {
    std::size_t node{}, host_slot{};
    float elapsed{};
    std::uint8_t evaluation_skip{};
    bool entered{}, retained{}, event{};
  };
  const world::WorldClock& clock_;
  BehaviorSchedule& schedule_;
  BehaviorActions& actions_;
  const formats::BehaviorTreeDefinition* definition_{};
  BehaviorScoring* scoring_{};
  std::array<Slot, 4> slots_{};
  std::size_t depth_{};
  std::uint64_t sequence_{};
  std::uint8_t dispatch_skip_{};
  std::optional<std::size_t> pending_event_;
  std::size_t current_entry() const;
  void enter_event(std::size_t);
  void select(std::size_t branch, bool force, BehaviorActionState);
};
}  // namespace bw::game
