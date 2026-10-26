// The parent targeting owner restores context before consuming this frame's
// mode edge. Entering targeting requests its camera immediately; leaving does
// not mutate the independent post-fire/upper-animation weapon lifetime.
// Header I.1/E.4/MEM.9 contracts apply to both operations.
#include "player_targeting.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bw::game {
void PlayerTargeting::advance(float seconds, bool cycle_pressed,
                              bool fire_targeting, bool automatic_camera) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid targeting time");
  }
  // Shooter update62ebe0 precedes input62ed80. A completed grace is consumed
  // before the following update restores the ordinary camera context.
  if (!selected_) {
    if (camera_grace_ > 0) {
      camera_grace_ = std::max(0.0F, camera_grace_ - seconds);
    } else {
      camera_targeted_ = false;
    }
  }
  if (cycle_pressed) selected_ = !selected_;
  active_ = selected_ || fire_targeting;
  if (active_) {
    constexpr float kSourceCameraGrace = .01F;
    if (automatic_camera) camera_grace_ = kSourceCameraGrace;
    if (cycle_pressed || (automatic_camera && !camera_targeted_)) {
      camera_targeted_ = selected_;
    }
  }
}
void PlayerTargeting::enter_forced_action(bool ordinary_camera) noexcept {
  selected_ = active_ = false;
  if (ordinary_camera) camera_targeted_ = false;
  constexpr float kForcedPostureCameraGrace = .01F;
  camera_grace_ = kForcedPostureCameraGrace;
}
void PlayerTargeting::select_for_review(bool targeted) noexcept {
  selected_ = active_ = camera_targeted_ = targeted;
  camera_grace_ = 0;
}
}  // namespace bw::game
