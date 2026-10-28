// Original AI ground/retreat sensors over the immutable source collision bank.
// Borrow collision for one synchronous query; return copied coordinates and
// never move actors or advance clocks. Ground misses retain the native sentinel
// height. Retreat clips its raised segment before the original downward probe,
// then restores body radius. The radius is prepared from actor settings, not
// inferred from visual bounds. Queries allocate no memory. Core I.1
// (#ri-explicit) exposes each input; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keeps the hot path fixed-size. Both
// design corpora read. Native sensor/constructor evidence: first-combat-ai.md.
#pragma once
#include <array>

#include "bw/math/vector.hpp"
#include "bw/spatial/static_collision.hpp"
namespace bw::game {
// World-owned twenty-bucket source work schedule. Advance once before actor
// updates; each due sensor reserves the least-loaded future bucket. Values are
// frame counts, not seconds. No actor may advance this shared owner.
// Pure source support composition shared by movement admission and route jobs.
// Caller supplies the candidate center and independent sphere/lateral radii;
// this layer adds no look-ahead or scheduler state and never moves an actor.
struct NpcGroundSupportRequest {
  math::Vector3<float> center{}, direction{};
  float ground_range{}, maximum_step{}, sphere_radius{}, lateral_radius{};
};
enum class NpcGroundSupportStatus : std::uint8_t {
  clear,
  missing,
  obstructed,
  penetrating
};
struct NpcGroundSupportResult {
  math::Vector3<float> position{};
  NpcGroundSupportStatus status{NpcGroundSupportStatus::clear};
};
NpcGroundSupportResult npc_ground_support(const scene::StaticCollision&,
                                          const NpcGroundSupportRequest&);
class NpcMovementSchedule {
 public:
  NpcMovementSchedule() = default;
  NpcMovementSchedule(const NpcMovementSchedule&) = delete;
  NpcMovementSchedule& operator=(const NpcMovementSchedule&) = delete;
  NpcMovementSchedule(NpcMovementSchedule&&) = delete;
  NpcMovementSchedule& operator=(NpcMovementSchedule&&) = delete;
  ~NpcMovementSchedule() = default;
  void advance();
  std::uint8_t reserve();

 private:
  static constexpr std::size_t kScheduleBuckets = 20;
  std::array<std::uint32_t, kScheduleBuckets> counts_{};
};
struct NpcMovementProbe {
  std::array<float, 3> origin{}, destination{};
  float body_radius{};
};
struct NpcMovementProbeResult {
  std::array<float, 3> destination{};
  bool supported{}, traversable{};
};
// Retained actor sensor flags begin true. Only its actor frame decrements the
// copied countdown; repeated action queries share one refresh. Shared spatial
// queries own all geometry, this owner only retains source scheduling/results.
class NpcMovementSensor {
 public:
  void advance();
  NpcMovementProbeResult query(const scene::StaticCollision&,
                               NpcMovementSchedule&, const NpcMovementProbe&);
  bool permitted() const { return supported_ && traversable_; }

 private:
  std::int32_t remaining_{};
  bool supported_{true}, traversable_{true};
};
std::array<float, 3> npc_ground_destination(const scene::StaticCollision&,
                                            const std::array<float, 3>& point,
                                            float up, float down);
// Source short forward probe reflects a sufficiently distant contact into a
// revised goal; ordinary motion still owns collision response.
std::array<float, 3> npc_approach_destination(
    const scene::StaticCollision&, const std::array<float, 3>& origin,
    const std::array<float, 3>& destination);
std::array<float, 3> npc_retreat_destination(
    const scene::StaticCollision&, const std::array<float, 3>& origin,
    const std::array<float, 3>& destination, float body_radius);
// Original dodge-ground sensor: cast ground (+2/-20), clip the candidate
// segment with .01 clearance, require >=3 units and an up-facing contact.
struct NpcDodgeProbe {
  std::array<float, 3> origin{}, destination{};
};
bool npc_dodge_clearance(const scene::StaticCollision&, const NpcDodgeProbe&);
}  // namespace bw::game
