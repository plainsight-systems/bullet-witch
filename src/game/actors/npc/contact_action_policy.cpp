// Preserve source contact-action priorities and slot publication. I.1 keeps
// observations explicit; MEM.9 retains fixed tables and value results. Both
// implementation corpora read. Source68ae10/66c970/66cca0; no rendering or
// collision authority belongs to this policy.
#include "contact_action_policy.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "bw/math/direction.hpp"
namespace bw::game {
std::array<unsigned, 3> npc_dodge_order(float angle) {
  if (!std::isfinite(angle)) throw std::invalid_argument("Invalid dodge angle");
  constexpr float quarter = .7853981852531433F;
  constexpr float half = 1.5707963705062866F;
  constexpr float three_quarters = 2.356194496154785F;
  const auto magnitude = std::abs(angle);
  if (magnitude < quarter) {
    return angle <= 0 ? std::array<unsigned, 3>{3, 1, 2}
                      : std::array<unsigned, 3>{2, 1, 3};
  }
  if (magnitude > three_quarters) {
    return angle <= 0 ? std::array<unsigned, 3>{3, 0, 2}
                      : std::array<unsigned, 3>{2, 0, 3};
  }
  if (angle < 0) {
    return magnitude <= half ? std::array<unsigned, 3>{0, 3, 1}
                             : std::array<unsigned, 3>{1, 3, 0};
  }
  return magnitude <= half ? std::array<unsigned, 3>{0, 2, 1}
                           : std::array<unsigned, 3>{1, 2, 0};
}
std::uint32_t npc_dodge_slot(const std::array<unsigned, 3>& order,
                             const std::array<bool, 4>& clear) {
  constexpr std::array<std::uint32_t, 4> slots{725, 724, 727, 726};
  for (const auto direction : order) {
    if (direction >= slots.size()) {
      throw std::invalid_argument("Invalid dodge direction");
    }
    if (clear.at(direction)) return slots.at(direction);
  }
  return slots[1];
}
std::optional<ActionMotionRequest> npc_grapple_motion(std::uint32_t mode) {
  constexpr std::uint32_t ordinary = 6, directed = 7, special = 10,
                          alternate = 11;
  constexpr std::uint32_t ordinary_slot = 722, directed_slot = 723,
                          special_slot = 830, alternate_slot = 808;
  std::uint32_t slot{};
  switch (mode) {
    case ordinary:
      slot = ordinary_slot;
      break;
    case directed:
      slot = directed_slot;
      break;
    case special:
      slot = special_slot;
      break;
    case alternate:
      slot = alternate_slot;
      break;
    default:
      return std::nullopt;
  }
  constexpr double blend = .3;
  return ActionMotionRequest{
      .slot = slot, .blend_seconds = blend, .fresh = true};
}
math::Vector3<float> npc_grapple_direction(
    const math::Vector3<float>& forward,
    const math::Vector3<float>& displacement, float maximum_angle) {
  if (!math::all_finite(forward) || !math::all_finite(displacement) ||
      !std::isfinite(maximum_angle) || maximum_angle < 0) {
    throw std::invalid_argument("Invalid grapple direction observation");
  }
  auto target = displacement;
  target[1] = 0;
  constexpr float minimum_squared = .000001F;
  const auto squared = math::dot(target, target);
  if (squared < minimum_squared) return forward;
  // Source66cde0 compares the unnormalized displacement, so a distant target
  // can snap sooner than a normalized angular controller would.
  if (std::cos(maximum_angle) <= math::dot(forward, target)) return target;
  const auto unit = math::scale(target, 1.F / std::sqrt(squared));
  const auto step = std::min(maximum_angle, std::numbers::pi_v<float>);
  return math::turn_toward(
      forward, unit, step,
      math::DirectionTurnPolicy<float>{
          {0, -1, 0}, 1.F - std::numeric_limits<float>::epsilon(), 0});
}
}  // namespace bw::game
