// Alicia raised-weapon action lifetime and facing policy. PlayerController owns
// the scalar lifetime; calls consume validated simulation seconds and accepted
// input in source update order. Invalid inputs throw before state mutation.
// No allocation; constant work. Numerical policy is preserved from the original
// raised-aim-posture evidence, independent of pose blending and target
// transforms. Both-phase Core I.1 (#ri-explicit), SF.11 (#rs-contained) keep
// action/pose ownership explicit; performance COPY.7 (eliminate-hidden-copies)
// retains small value inputs without copying actor/catalog state.
#include "raised_weapon_state.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bw::game {
namespace {
constexpr float kOrdinaryLifetime = 6, kHeavyLifetime = .75F;
constexpr std::uint32_t kHeavyWeapon = 3;
void validate_seconds(float value) {
  if (!std::isfinite(value) || value < 0) {
    throw std::invalid_argument("Invalid aim timestep");
  }
}
}  // namespace
float raised_turn_input(float look_x, std::uint8_t sensitivity, bool inverted) {
  constexpr std::uint8_t kMaximumSetting = 127;
  constexpr float kSettingOffset = 25, kSettingScale = .019999999552965164F;
  if (!std::isfinite(look_x) || std::abs(look_x) > 1 ||
      sensitivity > kMaximumSetting) {
    throw std::invalid_argument("Invalid raised look input");
  }
  const float value = look_x *
                      (static_cast<float>(sensitivity) + kSettingOffset) *
                      kSettingScale;
  return inverted ? -value : value;
}
bool RaisedWeaponLifetime::advance(float seconds, bool upper_present,
                                   bool accepted_refresh,
                                   std::uint32_t weapon_class) {
  validate_seconds(seconds);
  if (weapon_class > kHeavyWeapon) {
    throw std::invalid_argument("Invalid weapon class");
  }
  if (seconds_ > 0 && !upper_present) {
    seconds_ = std::max(0.0F, seconds_ - seconds);
  }
  if (accepted_refresh) {
    seconds_ =
        weapon_class == kHeavyWeapon ? kHeavyLifetime : kOrdinaryLifetime;
  }
  return active();
}
}  // namespace bw::game
