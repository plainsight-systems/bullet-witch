// Retain the original four joint slots and dispatch current completed centers.
// Both implementation corpora I.1/MEM.9 preserve the header's phase/ownership
// boundary. Scene segment feedback and actor overlap remain world consumers.
#include "melee_events.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bw::game {
MeleeMotionEvents::MeleeMotionEvents(std::uint64_t source,
                                     MeleeEmission& output)
    : source_(source), emission_(output) {
  if (source == 0) throw std::invalid_argument("Absent melee source identity");
}
void MeleeMotionEvents::reset() noexcept { slots_ = {}; }
void MeleeMotionEvents::consume(MeleeMotionCommand command,
                                animation::MotionEventPhase phase,
                                const math::Vector3<float>& actor_origin,
                                const math::Vector3<float>& completed_joint) {
  if (!math::all_finite(actor_origin) || !math::all_finite(completed_joint) ||
      !std::isfinite(command.radius) || command.radius < 0 ||
      !std::isfinite(command.damage) || command.damage < 0 ||
      !std::isfinite(command.far_damage) || command.far_damage < 0) {
    throw std::invalid_argument("Invalid melee motion publication");
  }
  if (phase == animation::MotionEventPhase::enter) {
    const auto empty = std::ranges::find(slots_, -1, &Slot::joint);
    if (empty == slots_.end()) return;
    const auto serial = emission_.get().begin_attack();
    if (serial == 0) throw std::logic_error("Invalid original attack serial");
    *empty = {command.joint,
              serial,
              {actor_origin[0], completed_joint[1], actor_origin[2]}};
    return;
  }
  for (auto& slot : slots_) {
    if (slot.joint != command.joint) continue;
    if (phase == animation::MotionEventPhase::exit ||
        phase == animation::MotionEventPhase::interrupt) {
      slot.joint = -1;
    } else {
      const auto previous = slot.previous;
      slot.previous = completed_joint;
      emission_.get().emit({source_, slot.serial, completed_joint, previous,
                            actor_origin, command.radius, command.damage,
                            command.far_damage, command.effect});
    }
  }
}
}  // namespace bw::game
