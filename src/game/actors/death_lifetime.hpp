// Original corpse retention policy shared by actor owners. A death state owns
// its six-second remaining delay; the world supplies admitted simulation delta
// and copied camera/actor observations. No actor, camera, renderer or physics
// owner is retained or destroyed here. Advance the delay, exempt camera-owned
// actors, then require a behind-camera position beyond the source family
// distance before requesting retirement. The world performs that request at
// its actor-lifetime boundary. Invalid/nonfinite inputs throw before mutation.
// Constant storage/work, no allocation or independent clock. Core I.11
// (#ri-raw) leaves lifetime authority outside this copied policy; performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds state.
// Both corpora read for design. Source5a54a0/683d40 and classification producer
// 47ef40/445580 are documented in first-combat-exchange.md.
#pragma once
#include <cstdint>

#include "bw/math/vector.hpp"

namespace bw::game {
struct DeathRetirementObservation {
  math::Vector3<float> position{}, camera_position{}, camera_forward{};
  std::uint32_t source_family_flags{};
  bool camera_owned{};
};
class DeathLifetime {
 public:
  void advance(float seconds);
  bool retirement_due(const DeathRetirementObservation&) const;

 private:
  static constexpr float kInitialDelaySeconds = 6;
  float remaining_seconds_{kInitialDelaySeconds};
};
}  // namespace bw::game
