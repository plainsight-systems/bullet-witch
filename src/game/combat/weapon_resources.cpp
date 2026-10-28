// Preserve source shooter transaction order, including partial emission before
// empty status. Core E.4 (#re-design-invariants) checks construction/delta
// before mutation; CDSA.32 prepares the interval instead of rediscovering it
// per shot. Both corpora retrieved again for this implementation phase.
#include "weapon_resources.hpp"

#include <cmath>
#include <stdexcept>
namespace bw::game {
WeaponResources::WeaponResources(unsigned capacity, float rate_per_second)
    : state_{capacity, capacity, 0}, interval_(1 / rate_per_second) {
  if (capacity == 0 || !std::isfinite(rate_per_second) ||
      rate_per_second <= 0 || !std::isfinite(interval_) || interval_ <= 0) {
    throw std::invalid_argument("Invalid weapon magazine or firing rate");
  }
}
WeaponShotReceipt WeaponResources::try_fire() {
  WeaponShotReceipt result{ShotStatus::accepted, 0, -state_.cooldown_seconds,
                           interval_};
  if (state_.cooldown_seconds > 0) {
    result.status = ShotStatus::cooling_down;
    return result;
  }
  // Catch up only the one retained interval debt, preserving terminal status.
  while (state_.cooldown_seconds <= 0) {
    if (state_.ammunition == 0) {
      result.status = ShotStatus::empty;
      break;
    }
    --state_.ammunition;
    ++result.shot_count;
    state_.cooldown_seconds += interval_;
  }
  return result;
}
void WeaponResources::advance(float admitted_seconds) {
  if (!std::isfinite(admitted_seconds) || admitted_seconds < 0) {
    throw std::invalid_argument("Invalid weapon cadence delta");
  }
  state_.cooldown_seconds = state_.cooldown_seconds > 0
                                ? state_.cooldown_seconds - admitted_seconds
                                : 0;
}
void WeaponResources::recharge() noexcept {
  state_.ammunition = state_.capacity;
  state_.cooldown_seconds = 0;
}
}  // namespace bw::game
