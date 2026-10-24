// Instance-owned motion event lifecycle, separate from pose sampling and game
// effect receivers. Borrows immutable prepared channels for its lifetime and
// owns fixed per-record state. One owner thread; no concurrent/reentrant calls.
// Receivers are borrowed synchronously, must not throw or mutate this timeline,
// and must keep their own effect state until reset/interruption is delivered.
// Algorithm: truncate source frame clocks; on wrap terminate active records;
// mark crossings in source order; dispatch enter/update/exit. Interruption uses
// a distinct callback, while reset also clears receiver-level latches. Repeated
// frame ticks update active intervals; pause callers must skip advance
// entirely. Source wrap consumes one observed clock wrap, never infers multiple
// cycles or reverse playback. Reset explicitly chooses the next starting clock.
// O(records) per tick/reset, fixed 128 states per channel, no allocation/copy
// of payloads. Invalid channels/clocks reject before effects/state change.
// Callback effects cannot be rolled back. Destruction is passive: owner must
// reset or interrupt live effects before receiver/resource teardown. Design:
// Core I.1 (#ri-explicit) makes clocks/lifetime/callback phases explicit;
// Performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keeps all runtime storage fixed. Source and pinned Unreal comparison are
// recorded in research/engine/subsystems/motion-event-timeline.md.
#pragma once

#include "bw/formats/motion_events.hpp"

namespace bw::animation {
enum class MotionEventPhase : std::uint8_t { enter, update, exit, interrupt };
struct MotionEventContext {
  std::size_t channel{}, record{};
  std::uint32_t tag{};
  formats::MotionEvent event;
  float previous_frame{}, current_frame{};
};
class MotionEventReceiver {
 public:
  MotionEventReceiver() = default;
  MotionEventReceiver(const MotionEventReceiver&) = delete;
  MotionEventReceiver& operator=(const MotionEventReceiver&) = delete;
  MotionEventReceiver(MotionEventReceiver&&) = delete;
  MotionEventReceiver& operator=(MotionEventReceiver&&) = delete;
  virtual ~MotionEventReceiver() = default;
  virtual void receive(MotionEventPhase phase,
                       const MotionEventContext& context) noexcept = 0;
  virtual void reset() noexcept = 0;
};
class EventTimeline {
 public:
  explicit EventTimeline(formats::MotionEventChannels channels,
                         float initial_frame = 0);
  EventTimeline(const EventTimeline&) = delete;
  EventTimeline& operator=(const EventTimeline&) = delete;
  EventTimeline(EventTimeline&&) = delete;
  EventTimeline& operator=(EventTimeline&&) = delete;
  ~EventTimeline() = default;
  void advance(float frame, MotionEventReceiver& receiver);
  // Active intervals receive interrupt, not ordinary end. Keeps frame clock.
  void interrupt(MotionEventReceiver& receiver) noexcept;
  // Interrupt first, then clear all receiver latches and select initial clock.
  // -1 may prime a source controller; its first nonnegative tick is suppressed.
  void reset(MotionEventReceiver& receiver, float initial_frame = 0);
  float current_frame() const { return current_frame_; }

 private:
  formats::MotionEventChannels channels_;
  std::array<std::array<bool, formats::kMotionEventsPerChannel>,
             formats::kMotionEventChannels>
      active_{};
  float previous_frame_{-1}, current_frame_{};
};
}  // namespace bw::animation
