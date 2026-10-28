// Source local navigation separates world-owned neighbor sensors from each
// route's retained heading. Controllers submit copied identity/position/heading
// and immutable settings; the world evaluates pending sensors once before AI,
// using shared collision visibility. A refreshed request retains its previous
// completed result until the next service phase. Retiring an actor removes its
// sensor by stable identity. All source route sensors share group zero;
// authored placement groups do not filter neighbors. Single simulation-thread
// ownership. Bounded fifty sensors, O(N²) neighbor enumeration, no steady-state
// allocation. F.7 (#rf-smart) keeps collision borrowed; MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) makes capacity
// explicit. Both design corpora read. Source algorithms and unknown graph
// boundary: first-combat-ai.
#pragma once
#include <array>
#include <cstdint>

#include "bw/math/vector.hpp"
#include "bw/spatial/static_collision.hpp"
namespace bw::game {
struct NpcSteeringSettings {
  float cohesion_cosine{}, cohesion_squared{}, separation_cosine{},
      separation_squared{}, avoidance_threshold{}, aggregate_weight{},
      cohesion_weight{}, alignment_weight{}, separation_weight{},
      avoidance_weight{};
};
struct NpcSteeringRequest {
  std::uint64_t identity{};
  math::Vector3<float> position{}, heading{};
  NpcSteeringSettings settings;
};
struct NpcSteeringSample {
  // Weighted unit contributions from one completed sensor evaluation.
  math::Vector3<float> cohesion{}, other{};
  math::Vector3<float> direction(bool include_cohesion) const;
};
class NpcSteeringSensors {
 public:
  NpcSteeringSensors() = default;
  ~NpcSteeringSensors() = default;
  NpcSteeringSensors(const NpcSteeringSensors&) = delete;
  NpcSteeringSensors& operator=(const NpcSteeringSensors&) = delete;
  NpcSteeringSensors(NpcSteeringSensors&&) = delete;
  NpcSteeringSensors& operator=(NpcSteeringSensors&&) = delete;
  NpcSteeringSample refresh(const NpcSteeringRequest&);
  void advance(const scene::StaticCollision&);
  void retire(std::uint64_t identity) noexcept;

 private:
  struct Entry {
    NpcSteeringRequest request;
    NpcSteeringSample sample;
    bool pending{};
  };
  static constexpr std::size_t kSensorCapacity = 50;
  std::array<Entry, kSensorCapacity> entries_{};
};
// Retained route direction and firing suppression share the authoritative world
// interval; no actor pose or movement mutation is granted to this owner.
class NpcSteeringHeading {
 public:
  void suppress_cohesion() noexcept { suppression_ = 1; }
  math::Vector3<float> advance(const math::Vector3<float>& goal_direction,
                               const NpcSteeringSample&, float aggregate_weight,
                               bool approach, float seconds);
  const math::Vector3<float>& heading() const noexcept { return heading_; }
  void reset(const math::Vector3<float>& heading);

 private:
  math::Vector3<float> heading_{0, 0, 1};
  float suppression_{};
};
}  // namespace bw::game
