// Preserve source gate and arithmetic order before the resource owner commits.
// Core I.1 and MEM.9 were retrieved again during implementation: no hidden
// actor policy or allocation enters this copied health transaction.
#include "health_damage.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bw::game {
HealthDamageResult apply_health_damage(const HealthDamageInput& input) {
  for (const float value : {input.current, input.recoverable, input.maximum,
                            input.magnitude, input.scale}) {
    if (!std::isfinite(value) || value < 0) {
      throw std::invalid_argument("Invalid health damage input");
    }
  }
  if (input.maximum <= 0 || input.current > input.maximum ||
      input.recoverable > input.maximum ||
      (input.mode != HealthDamageMode::ordinary &&
       input.mode != HealthDamageMode::player_camera)) {
    throw std::invalid_argument("Invalid health damage state");
  }
  HealthDamageResult result{input.current, input.recoverable, false,
                            input.invulnerable || input.current > 0};
  if (input.invulnerable || input.current <= 0) return result;
  const float scaled = input.magnitude * input.scale;
  constexpr float kPlayerCurrentMultiplier = 4;
  const float current_damage = input.mode == HealthDamageMode::player_camera
                                   ? scaled * kPlayerCurrentMultiplier
                                   : scaled;
  if (!std::isfinite(scaled) || !std::isfinite(current_damage)) {
    throw std::invalid_argument("Health damage exceeds finite range");
  }
  result.accepted = true;
  result.recoverable = std::max(0.F, input.recoverable - scaled);
  result.current = input.mode == HealthDamageMode::player_camera
                       ? std::max(0.F, input.current - current_damage)
                       : result.recoverable;
  if (input.nonlethal && result.current <= 0) {
    result.current = 1;
    result.recoverable = input.maximum;
  }
  result.alive = result.current > 0;
  return result;
}
}  // namespace bw::game
