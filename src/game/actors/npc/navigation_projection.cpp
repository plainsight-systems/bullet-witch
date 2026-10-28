// Apply the recovered sensor composition with shared segment geometry. I.1 and
// MEM.9 retain the header's query-only, allocation-free contract in both
// phases.
#include "navigation_projection.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "bw/math/vector.hpp"
namespace bw::game {
void NpcMovementSchedule::advance() {
  for (std::size_t i = 0; i + 1 < counts_.size(); ++i) {
    counts_.at(i) = counts_.at(i + 1);
  }
  counts_.back() = 0;
}
std::uint8_t NpcMovementSchedule::reserve() {
  auto selected = counts_.size() - 1;
  for (auto i = selected - 1; i > 0; --i) {
    if (counts_.at(i) < counts_.at(selected)) selected = i;
  }
  if (counts_.at(selected) == std::numeric_limits<std::uint32_t>::max()) {
    throw std::overflow_error("NPC movement schedule exhausted");
  }
  ++counts_.at(selected);
  return static_cast<std::uint8_t>(selected);
}
void NpcMovementSensor::advance() {
  // Negative countdowns have identical due semantics; avoid unbounded signed
  // subtraction while an actor remains in an action that never queries motion.
  if (remaining_ > 0) --remaining_;
}
namespace {
enum class SupportResult : std::uint8_t { clear, missing, obstructed };
SupportResult movement_support(const scene::StaticCollision& collision,
                               const NpcGroundSupportRequest& request,
                               std::array<float, 3>& sample) {
  const auto& center = request.center;
  constexpr float kClearance = .01F, kUpNormal = .6946583986282349F;
  const auto horizontal = collision.clip_segment({center, sample, 0, 0x3000});
  if (horizontal.clipped) {
    const auto direction = math::normalize_or(math::subtract(center, sample),
                                              std::array<float, 3>{});
    sample = math::add(horizontal.position, math::scale(direction, kClearance));
    if (horizontal.normal[1] <= kUpNormal) return SupportResult::obstructed;
  }
  auto from = sample, to = sample;
  from[1] += request.ground_range;
  to[1] -= request.ground_range;
  const auto ground = collision.clip_segment({from, to, 0, 0});
  if (!ground.clipped) {
    sample = to;
    return SupportResult::missing;
  }
  const auto rise = ground.position[1] - sample[1];
  sample = ground.position;
  return ground.actor_owned || rise > request.maximum_step
             ? SupportResult::obstructed
             : SupportResult::clear;
}
NpcMovementProbeResult movement_probe(const scene::StaticCollision& collision,
                                      const NpcMovementProbe& request) {
  auto result = NpcMovementProbeResult{request.destination, true, true};
  auto direction = math::subtract(request.destination, request.origin);
  direction[1] = 0;
  constexpr float kMinimumSquared = 1.0000001111620804e-6F;
  if (math::dot(direction, direction) < kMinimumSquared) return result;
  direction = math::normalize_or(direction, std::array<float, 3>{});
  constexpr float kLookAhead = .1F, kBodyHeight = .3F;
  auto center = math::add(request.origin, math::scale(direction, kLookAhead));
  const auto lateral_radius = std::sqrt(
      request.body_radius * request.body_radius - kLookAhead * kLookAhead);
  constexpr float kGroundRange = 2;
  const auto support = npc_ground_support(
      collision, {center, direction, kGroundRange, kBodyHeight,
                  request.body_radius, lateral_radius});
  if (support.status != NpcGroundSupportStatus::clear) {
    result.supported = result.traversable = false;
    if (support.status == NpcGroundSupportStatus::missing) {
      result.destination = request.origin;
    } else if (support.status == NpcGroundSupportStatus::penetrating) {
      result.destination = support.position;
    }
  }
  return result;
}
}  // namespace
NpcGroundSupportResult npc_ground_support(
    const scene::StaticCollision& collision,
    const NpcGroundSupportRequest& request) {
  if (!math::all_finite(request.center) ||
      !math::all_finite(request.direction) ||
      !std::isfinite(request.ground_range) || request.ground_range < 0 ||
      !std::isfinite(request.maximum_step) || request.maximum_step < 0 ||
      !std::isfinite(request.sphere_radius) || request.sphere_radius <= 0 ||
      !std::isfinite(request.lateral_radius) || request.lateral_radius < 0) {
    throw std::invalid_argument("Invalid shared NPC ground support query");
  }
  const auto& center = request.center;
  const auto& direction = request.direction;
  const auto side =
      math::normalize_or(math::Vector3<float>{direction[2], 0, -direction[0]},
                         math::Vector3<float>{});
  std::array samples{
      math::add(center, math::scale(side, request.lateral_radius)),
      math::subtract(center, math::scale(side, request.lateral_radius)),
      math::add(center, math::scale(direction, request.lateral_radius)),
      math::subtract(center, math::scale(direction, request.lateral_radius))};
  bool missing = false, obstructed = false;
  for (auto& sample : samples) {
    const auto support = movement_support(collision, request, sample);
    missing = missing || support == SupportResult::missing;
    obstructed = obstructed || support == SupportResult::obstructed;
  }
  if (missing) return {center, NpcGroundSupportStatus::missing};
  if (obstructed) return {center, NpcGroundSupportStatus::obstructed};
  auto sphere = center;
  sphere[1] += request.maximum_step + request.sphere_radius;
  constexpr std::uint32_t kBodyExclusions = 0x1000;
  const auto penetration = collision.sphere_penetration(
      sphere, request.sphere_radius, kBodyExclusions);
  if (penetration.penetrating) {
    auto corrected = math::add(center, penetration.translation);
    corrected[1] += request.sphere_radius;
    return {corrected, NpcGroundSupportStatus::penetrating};
  }
  return {center, NpcGroundSupportStatus::clear};
}
NpcMovementProbeResult NpcMovementSensor::query(
    const scene::StaticCollision& collision, NpcMovementSchedule& schedule,
    const NpcMovementProbe& request) {
  constexpr float kMinimumRadius = .1F;
  if (!math::all_finite(request.origin) ||
      !math::all_finite(request.destination) ||
      !std::isfinite(request.body_radius) ||
      request.body_radius < kMinimumRadius) {
    throw std::invalid_argument("Invalid NPC movement sensor input");
  }
  if (remaining_ > 0) return {request.destination, supported_, traversable_};
  remaining_ = schedule.reserve();
  const auto result = movement_probe(collision, request);
  supported_ = result.supported;
  traversable_ = result.traversable;
  return result;
}
std::array<float, 3> npc_ground_destination(
    const scene::StaticCollision& collision, const std::array<float, 3>& point,
    float up, float down) {
  if (!math::all_finite(point) || !std::isfinite(up) || !std::isfinite(down) ||
      up < 0 || down < 0) {
    throw std::invalid_argument("Invalid NPC ground projection");
  }
  auto from = point, to = point;
  from[1] += up;
  to[1] -= down;
  const auto hit = collision.clip_segment({from, to, 0, 0});
  if (hit.clipped) return hit.position;
  constexpr float kMissingGround = -1000000.F;
  auto result = point;
  result[1] = kMissingGround;
  return result;
}
std::array<float, 3> npc_approach_destination(
    const scene::StaticCollision& collision, const std::array<float, 3>& origin,
    const std::array<float, 3>& destination) {
  if (!math::all_finite(origin) || !math::all_finite(destination)) {
    throw std::invalid_argument("Invalid NPC approach projection");
  }
  constexpr float kProbeDistance = 2, kReflectionScale = .5F;
  constexpr float kMinimumSquared = 1.0000001111620804e-6F;
  const auto offset = math::subtract(destination, origin);
  const auto squared = math::dot(offset, offset);
  if (squared < kMinimumSquared) return destination;
  const auto end = math::add(
      origin, math::scale(offset, kProbeDistance / std::sqrt(squared)));
  constexpr std::uint32_t kExcluded = 0x4000;
  const auto hit = collision.clip_segment({origin, end, .01F, kExcluded});
  if (!hit.clipped) return destination;
  const auto traveled = math::subtract(hit.position, origin);
  const auto distance_squared = math::dot(traveled, traveled);
  const auto normal_squared = math::dot(hit.normal, hit.normal);
  // Preserve the original squared-distance versus probe-distance predicate.
  if (distance_squared < kProbeDistance || normal_squared < kMinimumSquared) {
    return destination;
  }
  const auto distance = std::sqrt(distance_squared);
  const auto reflected =
      math::reflect(math::divide(traveled, distance),
                    math::divide(hit.normal, std::sqrt(normal_squared)));
  return math::add(hit.position,
                   math::scale(reflected, distance * kReflectionScale));
}
std::array<float, 3> npc_retreat_destination(
    const scene::StaticCollision& collision, const std::array<float, 3>& origin,
    const std::array<float, 3>& destination, float body_radius) {
  if (!math::all_finite(origin) || !math::all_finite(destination) ||
      !std::isfinite(body_radius) || body_radius < 0) {
    throw std::invalid_argument("Invalid NPC retreat projection");
  }
  // The AI sensor constructor supplies these offsets. Actor admission replaces
  // its radius fields, so radius + height - radius leaves the fixed height.
  constexpr float kSegmentHeight = .3F, kGroundStart = 2, kGroundDown = 20;
  constexpr std::uint32_t kClearanceExcluded = 0x1000;
  auto from = origin, to = destination;
  from[1] += kSegmentHeight;
  to[1] += kSegmentHeight;
  const auto hit = collision.clip_segment({from, to, 0, kClearanceExcluded});
  auto result = destination;
  if (hit.clipped) {
    // Source uses a ray hit followed by distance along the segment. Plane-
    // incidence clearance would move an oblique hit by a different amount.
    const auto direction =
        math::normalize_or(math::subtract(to, from), std::array<float, 3>{});
    result = math::subtract(hit.position, math::scale(direction, body_radius));
  }
  result[1] = origin[1] + kGroundStart;
  result = npc_ground_destination(collision, result, 0, kGroundDown);
  result[1] += body_radius;
  return result;
}
bool npc_dodge_clearance(const scene::StaticCollision& collision,
                         const NpcDodgeProbe& probe) {
  const auto& [origin, destination] = probe;
  constexpr float up = 2, down = 20, minimum_height = -10000.F;
  constexpr float clearance = .01F, minimum_distance_squared = 9;
  constexpr float contact_up = .6946584F;
  const auto ground = npc_ground_destination(collision, destination, up, down);
  if (ground[1] < minimum_height) return false;
  const auto clipped = collision.clip_segment({origin, ground, 0, 0});
  auto corrected = clipped.position;
  if (clipped.clipped) {
    const auto toward_origin = math::subtract(origin, ground);
    const auto length = math::length(toward_origin);
    if (length > 0) {
      corrected =
          math::add(corrected, math::scale(toward_origin, clearance / length));
    }
  }
  const auto delta = math::subtract(corrected, origin);
  return math::dot(delta, delta) >= minimum_distance_squared &&
         (!clipped.clipped || clipped.normal[1] > contact_up);
}
}  // namespace bw::game
