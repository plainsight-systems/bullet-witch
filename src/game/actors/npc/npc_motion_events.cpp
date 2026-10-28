// Copy source receiver output for the actor's post-pose presentation phase.
// I.11/CDSA.32 were retrieved for implementation; no callback escapes lifetime
// or allocates. A full receipt buffer is an explicit error, never lost
// feedback.
#include "npc_motion_events.hpp"

#include <stdexcept>
namespace bw::game {
void NpcMotionEvents::append(NpcPresentationCommand command,
                             animation::MotionEventPhase phase,
                             animation::MotionEventContext context) noexcept {
  if (count_ == events_.size()) {
    overflow_ = true;
    return;
  }
  events_.at(count_++) = {command, phase, context};
}
void NpcMotionEvents::visibility(VisibilityMotionCommand command) noexcept {
  append(MotionPresentationCommand{command}, {});
}
void NpcMotionEvents::facial(FacialMotionCommand command) noexcept {
  append(MotionPresentationCommand{command}, {});
}
void NpcMotionEvents::voice(VoiceMotionCommand command) noexcept {
  append(MotionPresentationCommand{command}, {});
}
void NpcMotionEvents::sound(
    SoundMotionCommand command, animation::MotionEventPhase phase,
    const animation::MotionEventContext& context) noexcept {
  append(MotionPresentationCommand{command}, phase, context);
}
void NpcMotionEvents::melee(
    MeleeMotionCommand command, animation::MotionEventPhase phase,
    const animation::MotionEventContext& context) noexcept {
  append(MotionPresentationCommand{command}, phase, context);
}
void NpcMotionEvents::unsupported(
    animation::MotionEventPhase phase,
    const animation::MotionEventContext& context) noexcept {
  append(MotionPresentationCommand{UnsupportedMotionCommand{}}, phase, context);
}
void NpcMotionEvents::reset_presentation() noexcept {
  append(NpcPresentationReset{}, {});
}
std::span<const NpcMotionEvent> NpcMotionEvents::pending() const {
  if (overflow_) throw std::length_error("NPC motion event capacity exhausted");
  return std::span(events_).first(count_);
}
void NpcMotionEvents::consume() noexcept {
  count_ = 0;
  overflow_ = false;
}
}  // namespace bw::game
