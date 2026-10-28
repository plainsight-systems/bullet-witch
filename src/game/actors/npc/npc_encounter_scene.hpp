// Read-only encounter observations supplied by the owning world. Actor order is
// original traversal order, with the player first; stable identities must be
// nonzero. Spans borrow frame-stable world storage and are consumed
// synchronously before population mutation. No actor, physics-step,
// projectile-lifetime or clock mutation escapes this interface.
// Controller-owned queries combine these facts with shared collision and the
// immutable navigation archive. Missing actors remain absent; unavailable
// authored paths return an empty span, not a fabricated route. Scene light is
// the current sampled original stage value. Core I.1 (#ri-explicit) separates
// observations from actor commands; performance CDSA.32 prepares contiguous
// snapshots once for all actor readers. Both design corpora read.
// first-combat-ai.md describes source meanings.
#pragma once
#include "perception.hpp"
#include "scene_navigation.hpp"
#include "steering.hpp"
namespace bw::game {
struct NpcEncounterActor {
  PerceptionActor perception;
  math::Vector3<float> forward{}, velocity{};
  math::Vector3<float> aim_position{};
  float maximum_life{};
  std::int32_t group{}, model_category{-1};
  bool leader{};
};
// Copied source messages are submitted to the world dispatcher. The issuer
// cannot enumerate/mutate other controller owners; the world preserves original
// active-list delivery order and filters before calling a receiving controller.
struct NpcTargetTransmission {
  std::uint64_t sender{};
  PerceptionState target;
  math::Vector3<float> origin{};
  std::uint32_t affiliation{};
  std::int32_t group{}, recipients{};
  float near_strength{}, far_strength{}, radius{}, duration{};
};
struct NpcBehaviorTransmission {
  std::uint64_t sender{};
  std::optional<std::uint64_t> direct_target;
  math::Vector3<float> origin{};
  std::uint32_t affiliation{};
  std::int32_t group{}, recipients{};
  float radius{};
  std::uint32_t event{};
};
class NpcEncounterCommands {
 public:
  NpcEncounterCommands() = default;
  virtual ~NpcEncounterCommands() = default;
  NpcEncounterCommands(const NpcEncounterCommands&) = delete;
  NpcEncounterCommands& operator=(const NpcEncounterCommands&) = delete;
  NpcEncounterCommands(NpcEncounterCommands&&) = delete;
  NpcEncounterCommands& operator=(NpcEncounterCommands&&) = delete;
  // Synchronous world-owned admission; controllers receive no timer mutation.
  virtual NpcSteeringSample refresh_steering(const NpcSteeringRequest&) = 0;
  virtual bool admit_reaction(std::uint32_t motion_slot, bool bypass) = 0;
  virtual void share_target(const NpcTargetTransmission&) = 0;
  virtual void broadcast_behavior(const NpcBehaviorTransmission&) = 0;
};
class NpcEncounterScene {
 public:
  NpcEncounterScene() = default;
  virtual ~NpcEncounterScene() = default;
  NpcEncounterScene(const NpcEncounterScene&) = delete;
  NpcEncounterScene& operator=(const NpcEncounterScene&) = delete;
  NpcEncounterScene(NpcEncounterScene&&) = delete;
  NpcEncounterScene& operator=(NpcEncounterScene&&) = delete;
  virtual std::span<const NpcEncounterActor> actors() const = 0;
  virtual std::span<const NpcCoverPoint> cover_points() const = 0;
  virtual std::span<const NpcCoverObstacle> cover_obstacles() const = 0;
  virtual std::span<const math::Vector3<float>> patrol_points(
      std::string_view primary_path, std::string_view secondary_path,
      std::int32_t selector) const = 0;
  virtual math::Vector3<float> light_color() const = 0;
};
}  // namespace bw::game
