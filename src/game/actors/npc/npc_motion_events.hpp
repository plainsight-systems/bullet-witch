// NPC motion events are copied into bounded per-instance receipts after pose
// evaluation. The actor consumes them synchronously before the next advance;
// no callback owns an actor, audio output, clock or resource. Preserve order
// and phase and context, including scoped reset, rather than dropping unknown
// presentation. Context payload spans borrow immutable mission motion assets
// through synchronous consumption; actors must drain before resource teardown.
// Noexcept engine callbacks record overflow; the actor's checked read throws.
// Sixteen receipt slots are explicit admission capacity, not silent truncation.
// O(events), fixed storage, no frame allocation. Core I.11 (#ri-raw) and
// performance CDSA.32 (transform-static-data-into-the-consumer-layout) keep
// prepared commands and mutable receipts separate. Both corpora read for
// design. Original current-only receiver routing: motion-event-timeline.md.
#pragma once
#include "game/actors/animation/motion_presentation.hpp"
namespace bw::game {
struct NpcPresentationReset {};
using NpcPresentationCommand =
    std::variant<MotionPresentationCommand, NpcPresentationReset>;
struct NpcMotionEvent {
  NpcPresentationCommand command;
  animation::MotionEventPhase phase{};
  animation::MotionEventContext context{};
};
class NpcMotionEvents final : public MotionPresentationSink {
 public:
  void visibility(VisibilityMotionCommand) noexcept override;
  void facial(FacialMotionCommand) noexcept override;
  void voice(VoiceMotionCommand) noexcept override;
  void sound(SoundMotionCommand, animation::MotionEventPhase,
             const animation::MotionEventContext&) noexcept override;
  void melee(MeleeMotionCommand, animation::MotionEventPhase,
             const animation::MotionEventContext&) noexcept override;
  void unsupported(animation::MotionEventPhase,
                   const animation::MotionEventContext&) noexcept override;
  void reset_presentation() noexcept override;
  std::span<const NpcMotionEvent> pending() const;
  void consume() noexcept;

 private:
  static constexpr std::size_t kCapacity = 16;
  std::array<NpcMotionEvent, kCapacity> events_{};
  std::size_t count_{};
  bool overflow_{};
  void append(NpcPresentationCommand, animation::MotionEventPhase,
              animation::MotionEventContext context = {}) noexcept;
};
}  // namespace bw::game
