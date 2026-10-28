// Preserve shared reaction cooldown identity independently of any actor's pose.
// E.4 (#re-design-invariants) validates time before mutation; CDSA.9
// (guidelines/cpu-dsa/CDSA.9-pick-search-layout-by-cache-level.md) keeps the
// ten ordered entries contiguous. Both implementation corpora retrieved. Source
// replacement ties keep the earlier slot; an existing bypass never refreshes.
#include "reaction_admission.hpp"

#include <cmath>
#include <stdexcept>
namespace bw::game {
bool NpcReactionAdmission::admit(std::uint32_t motion_slot, bool bypass) {
  auto* candidate = &entries_.front();
  for (auto& entry : entries_) {
    if (entry.active && entry.slot == motion_slot) return bypass;
    if (candidate->active &&
        (!entry.active || entry.remaining < candidate->remaining)) {
      candidate = &entry;
    }
  }
  constexpr float kRetentionSeconds = 10;
  *candidate = {motion_slot, kRetentionSeconds, true};
  return true;
}
void NpcReactionAdmission::advance(float seconds) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid shared reaction clock interval");
  }
  for (auto& entry : entries_) {
    if (!entry.active) continue;
    entry.remaining -= seconds;
    if (entry.remaining <= 0) entry.active = false;
  }
}
}  // namespace bw::game
