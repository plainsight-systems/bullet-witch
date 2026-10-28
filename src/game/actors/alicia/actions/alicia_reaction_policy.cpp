// Hardware melee selection applies both-phase I.1 (#ri-explicit) to the
// caller-owned admission/basis contract and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) to fixed shared-math computations.
// Source reaction timers and action lifetimes; the caller owns presentation
// capabilities and applies plans before continuing grounded actor updates.
// Both MCP implementation corpora: E.4 validates before state writes; I.4
// separates frame crossings from completions; MEM.9 keeps updates allocation
// free. Source-specific equations are mapped in alicia-action-transitions.md.
#include "alicia_reaction_policy.hpp"

#include <cmath>
#include <span>
#include <stdexcept>

namespace bw::game {
namespace {
constexpr std::uint32_t kSlotCount = 1108;
constexpr std::array<std::uint32_t, 3> kAttacks{706, 707, 708};
constexpr float kMinimumDirectionSquared = 1.0000001111620804e-6F;
constexpr double kMagicStartFrame = 1;
ActionMotionPlan plan(std::uint32_t slot) {
  ActionMotionPlan result;
  result.count = 1;
  result.requests[0].slot = slot;
  result.requests[0].fresh = true;
  return result;
}
std::uint32_t attack_slot(CloseAttackVariant variant) {
  const auto index = static_cast<std::size_t>(variant);
  if (index >= kAttacks.size()) {
    throw std::invalid_argument("Unknown close attack variant");
  }
  return kAttacks.at(index);
}
}  // namespace
// Hardware direction chooses a presentation variant only after caller
// admission. The original reciprocal-then-scale order and strict cone boundary
// are retained.
CloseAttackVariant select_close_attack_variant(
    const math::Vector3<float>& processed_movement,
    const math::Matrix4<float>& camera_basis,
    const math::Vector3<float>& model_forward) {
  if (!math::all_finite(processed_movement) ||
      !math::all_finite(model_forward)) {
    throw std::invalid_argument("Nonfinite close attack direction");
  }
  for (const auto& row : camera_basis) {
    if (!math::all_finite(row)) {
      throw std::invalid_argument("Nonfinite close attack camera basis");
    }
  }
  const auto squared = math::dot(processed_movement, processed_movement);
  if (!std::isfinite(squared)) {
    throw std::invalid_argument("Close attack direction magnitude overflow");
  }
  if (!(squared > 0) || squared < kMinimumDirectionSquared) {
    return CloseAttackVariant::ordinary;
  }
  const auto direction =
      math::scale(processed_movement, 1.0F / std::sqrt(squared));
  const auto world = math::transform_direction<float>(direction, camera_basis);
  const auto alignment = math::dot(model_forward, world);
  if (!std::isfinite(alignment)) {
    throw std::invalid_argument("Close attack direction transform overflow");
  }
  constexpr double kDirectedConeRadians = .7853981852531433;
  return alignment > static_cast<float>(std::cos(kDirectedConeRadians))
             ? CloseAttackVariant::directed
             : CloseAttackVariant::ordinary;
}

ActionMotionPlan CloseAttackPresentation::begin(CloseAttackVariant variant) {
  const auto selected = attack_slot(variant);
  slot_ = selected;
  pending_.reset();
  active_ = true;
  return plan(slot_);
}
void CloseAttackPresentation::queue_followup(CloseAttackVariant variant) {
  const auto selected = attack_slot(variant);
  if (!active_) throw std::logic_error("Close attack is inactive");
  pending_ = selected;
}
ActionMotionPlan CloseAttackPresentation::take_followup(bool frame_crossed) {
  if (!active_ || !pending_ || !frame_crossed) return {};
  slot_ = *pending_;
  pending_.reset();
  return plan(slot_);
}
CloseAttackUpdate CloseAttackPresentation::advance(
    const CloseAttackObservation& observation) {
  CloseAttackUpdate result;
  if (!active_) return result;
  if (observation.motion_completed || observation.cancellation_open) {
    result.cancelled = !observation.motion_completed;
    if (result.cancelled) result.exited_slot = slot_;
    interrupt();
  }
  result.active = active_;
  return result;
}
void CloseAttackPresentation::interrupt() noexcept {
  active_ = false;
  pending_.reset();
}

MagicPresentationUpdate MagicPresentation::begin(
    const AcceptedMagicPresentation& accepted) {
  if (accepted.slot >= kSlotCount || !accepted.shooter_update.has_value()) {
    throw std::invalid_argument(
        "Unknown admitted magic motion or Shooter update contract");
  }
  if (active_) {
    throw std::logic_error("Interrupt prior magic scope before replacement");
  }
  scope_ = accepted;
  active_ = true;
  auto request = plan(scope_.slot);
  request.requests[0].start = ActionMotionStart::frames;
  request.requests[0].start_frames = kMagicStartFrame;
  return {request, true, false};
}
MagicPresentationUpdate MagicPresentation::advance(
    bool current_motion_completed) {
  if (active_ && scope_.complete_with_motion && current_motion_completed) {
    return interrupt();
  }
  return {{}, active_, false};
}
MagicPresentationUpdate MagicPresentation::interrupt() noexcept {
  const bool release = active_;
  active_ = false;
  return {{}, false, release};
}
}  // namespace bw::game
