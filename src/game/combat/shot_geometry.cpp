// Consume original shot observations in source order, without acquiring world
// authority. Validate geometry before the borrowed session RNG advances.
// Implementation Core I.5 (#ri-pre) makes degenerate directions explicit;
// performance CDSA.32 (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-
// the-consumer-layout.md) consumes prepared spread scalars. Both implementation
// corpora queried/read; header carries ownership and cost contracts.
#include "shot_geometry.hpp"

#include <algorithm>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "bw/math/basis.hpp"
#include "bw/math/quaternion.hpp"
#include "game/session/compatibility/source_random.hpp"

namespace bw::game {
namespace {
using Vector = math::Vector3<float>;
constexpr Vector kUp{0, 1, 0};
constexpr math::BasisFallback<float> kBasisPolicy{
    .999F, std::numeric_limits<float>::epsilon(), kUp, {0, 0, -1}, {1, 0, 0}};
void finite(const Vector& value) {
  if (!math::all_finite(value)) {
    throw std::invalid_argument("Nonfinite shot observation");
  }
}
void positive(float value) {
  if (!std::isfinite(value) || value <= 0) {
    throw std::invalid_argument("Invalid positive shot scalar");
  }
}
void unit(const Vector& value) {
  finite(value);
  constexpr float kUnitTolerance = 0.001F;
  if (std::abs(math::dot(value, value) - 1) > kUnitTolerance) {
    throw std::invalid_argument("Shot axis is not unit length");
  }
}
void valid(const ShotGeometry& value) {
  finite(value.origin);
  finite(value.target);
  finite(value.scenery_reference);
  const auto delta = math::subtract(value.target, value.origin);
  positive(math::length(delta));
}
}  // namespace
ShotSegment player_shot_segment(const PlayerShotView& view) {
  finite(view.camera_position);
  finite(view.actor_position);
  unit(view.camera_forward);
  positive(view.range);
  constexpr float kActorDepthClearance = 0.1F;
  const float depth =
      math::dot(view.camera_forward,
                math::subtract(view.actor_position, view.camera_position)) +
      kActorDepthClearance;
  ShotSegment result{
      math::add(view.camera_position, math::scale(view.camera_forward, depth)),
      math::add(view.camera_position,
                math::scale(view.camera_forward, view.range))};
  finite(result.start);
  finite(result.end);
  positive(math::length(math::subtract(result.end, result.start)));
  return result;
}
ShotGeometry weapon_forward_shot_geometry(const math::Affine3<float>& bone) {
  for (const auto& row : bone) finite(row);
  ShotGeometry result{bone[3], math::add(bone[3], bone[2]), bone[3]};
  valid(result);
  return result;
}
ShotGeometry player_shot_geometry(const PlayerShotPlacement& input) {
  finite(input.muzzle);
  finite(input.target);
  finite(input.actor_position);
  finite(input.weapon_forward);
  positive(math::length(input.weapon_forward));
  positive(input.actor_radius);
  ShotGeometry result{input.muzzle, input.target,
                      input.aim_hit ? input.target : input.muzzle};
  // Restrict lateral separation relative to the actor-to-target plane first.
  const auto side =
      math::cross(kUp, math::subtract(input.target, input.actor_position));
  const float side_length = math::length(side);
  if (side_length > 0) {
    const auto offset = math::subtract(result.origin, input.actor_position);
    const float distance =
        std::abs(math::dot(offset, math::divide(side, side_length)));
    constexpr float kRadiusFraction = 0.9F;
    const float limit = input.actor_radius * kRadiusFraction;
    if (distance > limit) {
      result.origin = math::add(input.actor_position,
                                math::scale(offset, limit / distance));
    }
  }
  // Advance the origin to the source weapon-axis actor plane. The independent
  // scenery reference still denotes the pre-correction muzzle or aim hit.
  float depth = math::dot(input.weapon_forward,
                          math::subtract(input.actor_position, result.origin));
  const float target_depth = math::dot(
      input.weapon_forward, math::subtract(input.target, result.origin));
  if (depth < target_depth) {
    constexpr float kForwardClearance = 0.5F;
    if (depth >= 0) depth += kForwardClearance;
    result.origin =
        math::add(result.origin, math::scale(input.weapon_forward, depth));
  }
  valid(result);
  return result;
}
bool weapon_spread_widens(std::size_t body_slot) noexcept {
  constexpr std::size_t kDirectionMask = 0xf, kDashFamily = 0x10,
                        kDashJumpFamily = 0x290;
  const auto family = body_slot & ~kDirectionMask;
  return family == kDashFamily || family == kDashJumpFamily;
}
float advance_weapon_spread(const WeaponSpread& profile, float current,
                            float seconds, bool widening) {
  if (!std::isfinite(profile.minimum_radians) ||
      !std::isfinite(profile.maximum_radians) || profile.minimum_radians < 0 ||
      profile.maximum_radians < profile.minimum_radians ||
      !std::isfinite(current) || current < profile.minimum_radians ||
      current > profile.maximum_radians || !std::isfinite(seconds) ||
      seconds < 0) {
    throw std::invalid_argument("Invalid weapon spread observation");
  }
  positive(profile.widen_seconds);
  positive(profile.recover_seconds);
  const float range = profile.maximum_radians - profile.minimum_radians;
  return widening
             ? std::min(profile.maximum_radians,
                        current + range / profile.widen_seconds * seconds)
             : std::max(profile.minimum_radians,
                        current - range / profile.recover_seconds * seconds);
}
Vector sample_shot_direction(const ShotGeometry& geometry, float spread,
                             SourceRandom& random) {
  valid(geometry);
  if (!std::isfinite(spread) || spread < 0 ||
      spread > std::numbers::pi_v<float>) {
    throw std::invalid_argument("Invalid shot spread");
  }
  const auto delta = math::subtract(geometry.target, geometry.origin);
  const auto direction = math::divide(delta, math::length(delta));
  // The original draws uniform polar angle, not uniform solid angle. Even a
  // zero-angle shot consumes both draws to preserve shared stream ordering.
  const float polar = spread * random.unit();
  const float azimuth =
      2 * std::numbers::pi_v<float> * random.unit() - std::numbers::pi_v<float>;
  const float sine = std::sin(polar);
  const Vector local{sine * std::sin(azimuth), std::cos(polar),
                     sine * std::cos(azimuth)};
  // Exact antipodes use negative row1 of the original up-axis basis. That
  // basis is (+X,-Z,+Y), so the recovered opposite rotation axis is +Z.
  const auto rotation = math::rotation_between(
      math::DirectionRotation<float>{kUp, direction, {0, 0, 1}});
  return math::transform_direction<float>(
      local, math::affine_transform(
                 math::TransformComponents<float>{rotation, {}, {1, 1, 1}}));
}
math::Matrix4<float> shot_forward_transform(const Vector& origin,
                                            const Vector& direction) {
  finite(origin);
  finite(direction);
  positive(math::length(direction));
  auto result =
      math::orthonormal_basis_with_fallback(direction, kUp, kBasisPolicy);
  math::set_translation(result, origin);
  return result;
}
}  // namespace bw::game
