// Alicia raised-weapon action lifetime and facing policy. PlayerController owns
// the scalar lifetime; calls consume validated simulation seconds and accepted
// input in source update order. Invalid inputs throw before state mutation.
// No allocation; constant work. Numerical policy is preserved from the original
// raised-aim-posture evidence, independent of pose blending and target
// transforms. Both-phase Core I.1 (#ri-explicit), SF.11 (#rs-contained) keep
// action/pose ownership explicit; performance COPY.7 (eliminate-hidden-copies)
// retains small value inputs without copying actor/catalog state.
#pragma once
#include <cstdint>

#include "game/actors/animation/aim_posture.hpp"
namespace bw::game {
// Original input+54 is processed look-stick X, not angular velocity. Accept the
// explicit 7-bit sensitivity setting and configured inversion; stick swapping
// and mouse override happen upstream in the input owner.
float raised_turn_input(float look_x, std::uint8_t sensitivity, bool inverted);
class RaisedWeaponLifetime {
 public:
  // Update decay before refreshing (source Shooter update then hardware input).
  // Active upper animation holds the lifetime. Refresh requires an already
  // accepted aiming condition; it is not inferred from raw firing input.
  bool advance(float seconds, bool upper_present, bool accepted_refresh,
               std::uint32_t weapon_class);
  bool active() const noexcept { return seconds_ > 0; }
  float remaining_seconds() const noexcept { return seconds_; }

 private:
  float seconds_{};
};
}  // namespace bw::game
