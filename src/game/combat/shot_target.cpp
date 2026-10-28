// Preserve source query ordering: scenery shortens the actor query, and only
// scenery receives clearance. The posture-only minimum aim distance does not
// alter the target passed to the gun. Implementation Core I.1/MEM.9 reread;
// weak read authority and fixed copied scratch enforce the header contract.
#include "shot_target.hpp"
namespace bw::game {
ShotTarget player_shot_target(const physics::Queries& queries,
                              const PlayerShotView& view,
                              ShotQueryBanks banks) {
  const auto segment = player_shot_segment(view);
  ShotTarget target{segment.end, {}};
  if (banks.scenery != 0) {
    target.hit = queries.ray({.start = segment.start,
                              .end = target.position,
                              .layers = banks.scenery,
                              .exclude_flags = kShotDisabledReceiverMask});
    if (target.hit) {
      constexpr float kSceneryClearance = .01F;
      const auto toward_start = math::subtract(segment.start, segment.end);
      const float length = math::length(toward_start);
      target.position =
          math::add(target.hit->position,
                    math::scale(toward_start, kSceneryClearance / length));
    }
  }
  if (banks.actors != 0) {
    if (const auto hit =
            queries.ray({.start = segment.start,
                         .end = target.position,
                         .layers = banks.actors,
                         .exclude_flags = kShotActorExclusionMask})) {
      target.hit = hit;
      target.position = hit->position;
    }
  }
  return target;
}
}  // namespace bw::game
